#include <Core/SdlSurfaceGuiRenderer.hpp>

#include <Core/SdlImGuiBackend.hpp>
#include <Core/Renderer.hpp>
#include <Core/ScreenGuiLayout.hpp>
#include <Core/SurfaceGuiDrawing.hpp>
#include <Editor/ViewportGeometry.hpp>
#include <Instances/Cube.hpp>
#include <Instances/GuiButton.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Workspace.hpp>
#include <Util/Color4.hpp>
#include <include/imgui/imgui.h>
#include <algorithm>
#include <limits>

namespace {
bool hasSdlContent(SurfaceGui& surface) {
    if (surface.hasRenderableOwnContent()) return true;
    for (const auto& [name, child] : surface.getChildren()) {
        (void)name;
        if (SurfaceGui::isRenderableDirectChild(child.get(), true)) return true;
    }
    return false;
}
}

SdlSurfaceGuiRenderer::SdlSurfaceGuiRenderer(SDL_GPUDevice& device) : m_device(&device) {}

SdlSurfaceGuiRenderer::~SdlSurfaceGuiRenderer() {
    if (!m_device) return;
    for (auto& [surface, entry] : m_entries) {
        (void)surface;
        if (entry.texture) SDL_ReleaseGPUTexture(m_device, entry.texture);
    }
}

bool SdlSurfaceGuiRenderer::bake(Workspace& workspace, SDL_GPUCommandBuffer& commandBuffer,
                                 SdlImGuiBackend& backend, const GameGuiDrawing::Resources& resources,
                                 std::string& error) {
    m_layers.clear();
    std::erase_if(m_entries, [this](const auto& item) {
        if (!item.second.owner.expired()) return false;
        if (item.second.texture) SDL_ReleaseGPUTexture(m_device, item.second.texture);
        return true;
    });
    for (SurfaceGui* surface : workspace.getRenderSurfaceGuis()) {
        if (!surface) {
            error = "Workspace contains an empty SurfaceGui render entry";
            return false;
        }
        if (!surface->Visible) continue;
        auto parent = surface->Parent.lock();
        auto* cube = dynamic_cast<Cube*>(parent.get());
        if (!cube || cube->Color.a < 1.0f) continue;
        if (!hasSdlContent(*surface)) continue;
        const auto face = static_cast<std::size_t>(surface->face);
        if (face >= 6) {
            error = "Invalid Face on " + surface->getFullPath();
            return false;
        }
        SurfaceGuiLayout layout;
        if (!computeSurfaceGuiLayout(*surface, layout)) continue;
        const auto signature = surface->computeRenderContentSignature(ImGui::GetFontSize(), true);
        auto& entry = m_entries[surface];
        const bool replace = !entry.texture || entry.width != layout.w || entry.height != layout.h;
        auto& target = m_layers[cube][face];
        if (!replace && entry.signature == signature) {
            target = entry.texture;
            continue;
        }
        SDL_GPUTexture* texture = entry.texture;
        if (replace) {
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = static_cast<Uint32>(layout.w);
            info.height = static_cast<Uint32>(layout.h);
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            texture = SDL_CreateGPUTexture(m_device, &info);
            if (!texture) {
                error = surface->getFullPath() + ": " + SDL_GetError();
                return false;
            }
        }
        ImDrawList drawList(ImGui::GetDrawListSharedData());
        drawList._ResetForNewFrame();
        drawList.PushClipRect(ImVec2(0, 0), ImVec2((float)layout.w, (float)layout.h), false);
        drawList.PushTexture(ImGui::GetIO().Fonts->TexRef);
        for (auto& [name, child] : surface->getChildren()) {
            (void)name;
            if (!SurfaceGui::isRenderableDirectChild(child.get(), true)) continue;
            auto* element = static_cast<ScreenGuiObject*>(child.get());
            const auto rect = ScreenGuiLayout::computeRect(*element, 0.0f, 0.0f,
                                                           layout.cW, layout.cH, {1.0f, 1.0f});
            GameGuiDrawing::drawContent(
                &drawList, element, layout.offX + rect.x * layout.scale,
                layout.offY + rect.y * layout.scale,
                rect.w * layout.scale, rect.h * layout.scale, resources);
        }
        drawList.PopTexture();
        drawList.PopClipRect();
        ImDrawData drawData{};
        drawData.Valid = true;
        drawData.DisplaySize = ImVec2((float)layout.w, (float)layout.h);
        drawData.FramebufferScale = ImVec2(1, 1);
        drawData.AddDrawList(&drawList);
        ImVector<ImTextureData*> atlasTextures;
        collectImGuiAtlasTextures(atlasTextures);
        drawData.Textures = &atlasTextures;
        const Color4 background = surface->BackgroundColor;
        const SDL_FColor clear = surface->hasRenderableOwnContent()
            ? SDL_FColor{background.r * background.a, background.g * background.a,
                         background.b * background.a, background.a}
            : SDL_FColor{0, 0, 0, 0};
        if (!backend.render(drawData, commandBuffer, *texture, error, false, clear)) {
            if (replace) SDL_ReleaseGPUTexture(m_device, texture);
            error = surface->getFullPath() + ": " + error;
            return false;
        }
        if (replace && entry.texture) SDL_ReleaseGPUTexture(m_device, entry.texture);
        entry = {surface->weak_from_this(), texture, layout.w, layout.h, signature};
        target = texture;
    }
    return true;
}

GuiButton* SdlSurfaceGuiRenderer::clickedButton(Workspace& workspace,
                                              const GameGuiRenderContext& context) const {
    if (m_layers.empty() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        !ImGui::IsWindowHovered()) return nullptr;
    const ScreenGuiLayout::Rect viewport{context.viewportX, context.viewportY,
                                         context.viewportWidth, context.viewportHeight};
    const ImVec2 pointer = ImGui::GetMousePos();
    if (!viewport.contains(pointer.x, pointer.y)) return nullptr;

    const float projectedWidth = context.projectionAspect * viewport.h;
    const auto ray = ViewportGeometry::makeScreenRay(
        context.cameraPosition, context.cameraForward, context.cameraRight, context.cameraUp,
        {(pointer.x - viewport.x) * projectedWidth / viewport.w, pointer.y - viewport.y},
        {projectedWidth, viewport.h});
    BaseCube* nearest = nullptr;
    ViewportGeometry::ObbRayHit hit;
    float distance = std::numeric_limits<float>::infinity();
    for (BaseCube* cube : workspace.getRenderBaseCubes()) {
        if (!dynamic_cast<Cube*>(cube) || cube->Color.a < 1.0f ||
            !(cube->Size.x > 0.0f && cube->Size.y > 0.0f && cube->Size.z > 0.0f)) continue;
        const auto candidate = ViewportGeometry::raycastObb(ray, cube->getWorldCFrame(), cube->Size);
        if (candidate.hit && candidate.distance < distance) {
            hit = candidate;
            distance = candidate.distance;
            nearest = cube;
        }
    }
    const auto layers = m_layers.find(nearest);
    if (layers == m_layers.end()) return nullptr;

    constexpr Face faces[3][2] = {{Face::Left, Face::Right}, {Face::Bottom, Face::Top},
                                  {Face::Front, Face::Back}};
    const auto face = static_cast<std::size_t>(faces[hit.axis][hit.sign > 0.0f]);
    SDL_GPUTexture* target = layers->second[face];
    if (!target) return nullptr;
    const auto frame = nearest->getWorldCFrame();
    const Vector3 local = frame.Rotation.conjugate().rotate(
        ray.origin + ray.direction * distance - frame.Position) / nearest->Size;
    // Use the same face corners as the position/UV buffers sampled by the GPU.
    static const auto vertices = createCubeVertices(1.0f);
    const Vector3 topRight = vertices[face * 4].Position;
    const Vector3 topLeft = vertices[face * 4 + 3].Position;
    const Vector3 bottomLeft = vertices[face * 4 + 2].Position;
    const float u = Vector3::Dot(local - topLeft, topRight - topLeft);
    const float v = Vector3::Dot(local - topLeft, bottomLeft - topLeft);
    for (SurfaceGui* surface : workspace.getRenderSurfaceGuis()) {
        const auto entry = m_entries.find(surface);
        if (entry == m_entries.end() || entry->second.texture != target) continue;
        SurfaceGuiLayout layout;
        if (!computeSurfaceGuiLayout(*surface, layout)) return nullptr;
        const float x = (u * layout.w - layout.offX) / layout.scale;
        const float y = (v * layout.h - layout.offY) / layout.scale;
        GuiButton* button = nullptr;
        for (const auto& [name, child] : surface->getChildren()) {
            (void)name;
            auto* candidate = dynamic_cast<GuiButton*>(child.get());
            if (!candidate || !candidate->Visible || !candidate->Active) continue;
            const auto rect = ScreenGuiLayout::computeRect(*candidate, 0.0f, 0.0f,
                                                           layout.cW, layout.cH, {1.0f, 1.0f});
            if (rect.contains(x, y)) button = candidate;
        }
        return button;
    }
    return nullptr;
}
