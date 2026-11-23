#include <Core/SdlStudioViewportRenderer.hpp>

#include <Core/SdlScenePreview.hpp>
#include <Core/SdlBasicRenderer.hpp>
#include <Core/Renderer.hpp>
#include <Editor/ViewportSceneQueries.hpp>
#include <Instances/BaseCube.hpp>
#include <Math/Matrix4.hpp>
#include <include/stb_image.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {
std::vector<BaseCube*> collectViewportSelection(const ViewportRenderDesc& request) {
    std::vector<BaseCube*> result;
    if (!request.renderHighlights) {
        return result;
    }
    auto append = [&](Instance* selected) {
        if (!selected || selected->Parent.expired() || selected->IsA("Decal")) {
            return;
        }
        result.append_range(ViewportSceneQueries::collectHighlightBaseCubes(*selected));
    };
    append(request.primarySelection);
    if (request.selectionTargets) {
        for (Instance* selected : *request.selectionTargets) {
            if (selected != request.primarySelection) {
                append(selected);
            }
        }
    }
    return result;
}

Matrix4 studioViewProjection(const ViewportRenderDesc& request) {
    const float width = static_cast<float>(request.width);
    const float height = static_cast<float>(request.height);
    const float aspect = width / height;

    // Matrix4::Perspective uses OpenGL's [-1, 1] clip depth. SDL GPU uses
    // [0, 1], so apply the conversion at the renderer boundary.
    Matrix4 depthRange;
    depthRange.m[10] = 0.5f;
    depthRange.m[14] = 0.5f;
    const Matrix4 projection = depthRange * Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
    return projection * Matrix4::LookAt(request.cameraPosition,
                                        request.cameraPosition + request.cameraForward,
                                        request.cameraUp);
}
}

SdlStudioViewportRenderer::SdlStudioViewportRenderer(SDL_GPUDevice& device)
    : m_device(&device) {}

SdlStudioViewportRenderer::~SdlStudioViewportRenderer() {
    if (m_device && !m_ownedTextures.empty()) {
        SDL_WaitForGPUIdle(m_device);
        for (SDL_GPUTexture* texture : m_ownedTextures) {
            if (texture) SDL_ReleaseGPUTexture(m_device, texture);
        }
    }
}

void SdlStudioViewportRenderer::beginFrame(SDL_GPUCommandBuffer& commandBuffer, float elapsed) {
    if (m_frameActive) {
        reportError("SDL Studio viewport renderer: beginFrame called twice");
        return;
    }
    m_commandBuffer = &commandBuffer;
    m_elapsed = elapsed;
    m_error.clear();
    m_frameActive = true;
}

void SdlStudioViewportRenderer::endFrame() {
    if (!m_frameActive) {
        reportError("SDL Studio viewport renderer: endFrame without beginFrame");
        return;
    }
    m_commandBuffer = nullptr;
    m_frameActive = false;
    // The host calls this after submitting and retiring the frame. Keeping
    // these objects alive until here also keeps ImGui's texture references
    // valid while its draw data is being recorded.
    m_retiredScenes.clear();
}

bool SdlStudioViewportRenderer::loadWelcomeLogo(ImTextureRef& image, int& width, int& height,
                                                std::string& error) {
    image = ImTextureRef{};
    width = 0;
    height = 0;
    error.clear();
    if (!m_device) {
        error = "SDL Studio viewport renderer: GPU device is unavailable";
        return false;
    }
    int channels = 0;
    stbi_uc* pixels = stbi_load("assets/image/rcbnstudio.png", &width, &height, &channels,
                               STBI_rgb_alpha);
    if (!pixels || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        error = reason ? reason : "could not decode assets/image/rcbnstudio.png";
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    const std::size_t byteCount = static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> rgba(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    SDL_GPUTexture* texture = uploadSdlRgbaTexture(*m_device,
                                                    static_cast<unsigned>(width),
                                                    static_cast<unsigned>(height), rgba, error);
    if (!texture) return false;
    m_ownedTextures.push_back(texture);
    image = ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));
    return true;
}

ImTextureRef SdlStudioViewportRenderer::renderViewport(const ViewportPanel& panel,
                                                       const ViewportRenderDesc& request) {
    if (!m_frameActive || !m_commandBuffer) {
        reportError("SDL Studio viewport renderer: renderViewport outside a frame");
        return ImTextureRef{};
    }
    if (!request.workspace) {
        return ImTextureRef{};
    }
    if (request.width <= 0 || request.height <= 0) {
        reportError("SDL Studio viewport renderer: invalid viewport dimensions");
        return ImTextureRef{};
    }

    auto it = m_scenes.find(&panel);
    if (it == m_scenes.end()) {
        auto scene = std::make_unique<SdlLiveScene>();
        std::string sceneError;
        if (!scene->initialize(*m_device, sceneError)) {
            reportError("SDL Studio viewport renderer: " + sceneError);
            return ImTextureRef{};
        }
        it = m_scenes.emplace(&panel, std::move(scene)).first;
    }

    SdlLiveScene& scene = *it->second;
    std::string sceneError;
    if (!scene.sync(*request.workspace, sceneError)) {
        reportError("SDL Studio viewport renderer: " + sceneError);
        return ImTextureRef{};
    }
    const Matrix4 viewProjection = studioViewProjection(request);
    const std::vector<BaseCube*> selected = collectViewportSelection(request);
    if (!scene.draw(*request.workspace, viewProjection, request.cameraPosition,
                    request.cameraForward, m_elapsed, *m_commandBuffer,
                    static_cast<unsigned>(request.width), static_cast<unsigned>(request.height),
                    sceneError, selected)) {
        reportError("SDL Studio viewport renderer: " + sceneError);
        return ImTextureRef{};
    }

    SDL_GPUTexture* texture = scene.colorTarget();
    if (!texture) {
        reportError("SDL Studio viewport renderer: scene produced no color target");
        return ImTextureRef{};
    }
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));
}

void SdlStudioViewportRenderer::removeViewport(const ViewportPanel& panel) {
    const auto it = m_scenes.find(&panel);
    if (it == m_scenes.end()) {
        return;
    }
    if (m_frameActive) {
        m_retiredScenes.push_back(std::move(it->second));
    }
    m_scenes.erase(it);
}

const std::string& SdlStudioViewportRenderer::error() const {
    return m_error;
}

void SdlStudioViewportRenderer::reportError(const std::string& message) {
    if (m_error.empty()) {
        m_error = message;
    }
}
