#include <Core/SdlScenePreview.hpp>

#include <Core/SceneRuntime.hpp>
#include <Core/SdlBasicRenderer.hpp>
#ifdef RECUBIN_SDL_GPU_STUDIO
#include <Core/SdlPreviewImGui.hpp>
#endif
#include <Instances/BaseCube.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/Workspace.hpp>
#include <Math/Matrix4.hpp>
#include <Util/PngWriter.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <memory>
#include <vector>

namespace {
constexpr unsigned PREVIEW_WIDTH = 1280;
constexpr unsigned PREVIEW_HEIGHT = 720;

struct PreviewGeometry {
    Matrix4 model;
    const MeshCube* mesh;
};

Matrix4 previewPerspective(float aspect, float nearPlane, float farPlane) {
    // Convert the engine projection's [-1, 1] clip depth to SDL GPU's [0, 1].
    Matrix4 depthRange;
    depthRange.m[10] = 0.5f;
    depthRange.m[14] = 0.5f;
    return depthRange * Matrix4::Perspective(60.0f, aspect, nearPlane, farPlane);
}

SDL_GPUTransferBuffer* recordReadback(SDL_GPUDevice* device, SDL_GPUCommandBuffer* command,
                                      SDL_GPUTexture* texture, unsigned width, unsigned height,
                                      std::string& error) {
    const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4;
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = static_cast<Uint32>(byteCount);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!transfer) {
        error = SDL_GetError();
        return nullptr;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        error = SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }
    SDL_GPUTextureRegion source{texture, 0, 0, 0, 0, 0, width, height, 1};
    SDL_GPUTextureTransferInfo destination{transfer, 0, width, height};
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);

    return transfer;
}

bool finishReadback(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer,
                    SDL_GPUFence* fence, unsigned width, unsigned height,
                    const std::filesystem::path& path, std::string& error) {
    if (!fence || !SDL_WaitForGPUFences(device, true, &fence, 1)) {
        error = SDL_GetError();
        if (fence) SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    auto* mapped = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    if (!mapped) {
        error = SDL_GetError();
        SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * height);
    for (unsigned y = 0; y < height; ++y) {
        std::copy_n(mapped + static_cast<std::size_t>(height - 1 - y) * rowBytes,
                    rowBytes, pixels.data() + static_cast<std::size_t>(y) * rowBytes);
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUFence(device, fence);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return PngWriter::writeRgba8(path.string(), static_cast<int>(width), static_cast<int>(height), pixels, &error);
}

#ifdef RECUBIN_SDL_GPU_STUDIO
SDL_GPUTexture* createStudioCaptureTarget(SDL_GPUDevice* device, unsigned width,
                                          unsigned height, std::string& error) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* target = SDL_CreateGPUTexture(device, &info);
    if (!target) error = SDL_GetError();
    return target;
}

struct StudioTextureDeleter {
    SDL_GPUDevice* device = nullptr;
    void operator()(SDL_GPUTexture* texture) const {
        if (device && texture) SDL_ReleaseGPUTexture(device, texture);
    }
};
#endif
}

int runSdlScenePreview(const std::string& scenePath,
                       const std::string& title,
                       SdlGpuDriver driver,
                       const std::optional<std::string>& captureDir,
                       bool studio) {
    const auto staged = SceneRuntime::stageSceneLoad(scenePath, {}, {});
    if (!staged) {
        std::cerr << "SDL scene preview: failed to load " << scenePath << ": " << staged.message << '\n';
        return 1;
    }
    const auto workspaces = SceneRuntime::collectWorkspaces(staged.system);
    if (workspaces.empty() || !workspaces.front()) {
        std::cerr << "SDL scene preview: scene has no Workspace: " << scenePath << '\n';
        return 1;
    }

    std::vector<SdlBasicCube> cubes;
    std::vector<PreviewGeometry> geometrySources;
    Vector3 minBounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Vector3 maxBounds(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    std::size_t skipped = 0;
    for (BaseCube* cube : workspaces.front()->getRenderBaseCubes()) {
        if (!cube) {
            std::cerr << "SDL scene preview: Workspace contains an empty render entry\n";
            return 1;
        }
        const auto* mesh = dynamic_cast<const MeshCube*>(cube);
        if ((!mesh && cube->renderShapeIndex() != 0) || cube->Color.a < 1.0f) {
            ++skipped;
            continue;
        }
        if (mesh && !mesh->isUsingFallback() &&
            (mesh->cpuVertices().empty() || mesh->cpuIndices().empty())) {
            std::cerr << "SDL scene preview: MeshCube '" << mesh->Name << "' has no loaded geometry\n";
            ++skipped;
            continue;
        }
        const CFrame world = cube->getWorldCFrame();
        const Matrix4 model = world.toMatrix4() * Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);
        const float halfExtent = cube->Size.length() * 0.5f;
        const Color4 color = cube->Color;
        if (!std::isfinite(halfExtent) || !std::ranges::all_of(model.m, [](float value) {
                return std::isfinite(value);
            })) {
            std::cerr << "SDL scene preview: invalid transform for Cube '" << cube->Name << "'\n";
            return 1;
        }
        if (!std::isfinite(color.r) || !std::isfinite(color.g) ||
            !std::isfinite(color.b) || !std::isfinite(color.a)) {
            std::cerr << "SDL scene preview: non-finite color for Cube '" << cube->Name << "'\n";
            return 1;
        }
        const Vector3 half(halfExtent, halfExtent, halfExtent);
        minBounds.x = std::min(minBounds.x, world.Position.x - half.x);
        minBounds.y = std::min(minBounds.y, world.Position.y - half.y);
        minBounds.z = std::min(minBounds.z, world.Position.z - half.z);
        maxBounds.x = std::max(maxBounds.x, world.Position.x + half.x);
        maxBounds.y = std::max(maxBounds.y, world.Position.y + half.y);
        maxBounds.z = std::max(maxBounds.z, world.Position.z + half.z);
        geometrySources.push_back({model, mesh});
        cubes.push_back({{}, {color.r, color.g, color.b, color.a}});
    }
    if (skipped != 0) {
        std::cerr << "SDL scene preview: skipped " << skipped << " unsupported or transparent shapes\n";
    }

    const Vector3 center = cubes.empty() ? Vector3(0, 0, 0) : minBounds * 0.5f + maxBounds * 0.5f;
    const float radius = cubes.empty() ? 1.0f : std::max(1.0f, (maxBounds - minBounds).length() * 0.5f);
    if (!std::isfinite(radius)) {
        std::cerr << "SDL scene preview: bounds exceed the supported camera range\n";
        return 1;
    }

    SdlGpuWindow window;
    SdlGpuWindowConfig config;
    config.title = title.c_str();
    config.width = PREVIEW_WIDTH;
    config.height = PREVIEW_HEIGHT;
    config.driver = driver;
    config.shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
    config.present = !captureDir.has_value();
    config.createWindowOffscreen = studio && captureDir.has_value();
    config.visible = !captureDir.has_value();
    std::string error;
    if (!window.initialize(config, &error)) {
        std::cerr << "SDL scene preview: GPU initialization failed: " << error << '\n';
        return 1;
    }
    SdlBasicRenderer renderer;
    if (!renderer.initialize(*window.device(), error)) {
        std::cerr << "SDL scene preview: renderer initialization failed: " << error << '\n';
        return 1;
    }
    for (std::size_t index = 0; index < geometrySources.size(); ++index) {
        const MeshCube* mesh = geometrySources[index].mesh;
        if (!mesh || mesh->isUsingFallback()) {
            continue;
        }
        std::vector<std::array<float, 3>> positions;
        positions.reserve(mesh->cpuVertices().size());
        for (const MeshVertex& vertex : mesh->cpuVertices()) {
            positions.push_back({vertex.Position.x, vertex.Position.y, vertex.Position.z});
        }
        const auto geometry = renderer.uploadMesh(positions, mesh->cpuIndices(), error);
        if (geometry == 0) {
            std::cerr << "SDL scene preview: could not upload MeshCube '" << mesh->Name
                      << "': " << error << '\n';
            return 1;
        }
        cubes[index].geometry = geometry;
    }

#ifdef RECUBIN_SDL_GPU_STUDIO
    std::unique_ptr<SdlPreviewImGui> studioUi;
    if (studio) {
        studioUi = std::make_unique<SdlPreviewImGui>();
        SDL_GPUTextureFormat uiFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if (!captureDir) {
            uiFormat = SDL_GetGPUSwapchainTextureFormat(window.device(), window.window());
            if (uiFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
                error = SDL_GetError();
                std::cerr << "SDL scene preview: could not query Studio swapchain format: "
                          << error << '\n';
                return 1;
            }
        }
        if (!studioUi->initialize(*window.window(), *window.device(), uiFormat, error)) {
            std::cerr << "SDL scene preview: ImGui initialization failed: " << error << '\n';
            return 1;
        }
    }
#endif

#ifdef RECUBIN_SDL_GPU_STUDIO
    std::unique_ptr<SDL_GPUTexture, StudioTextureDeleter> studioCaptureTarget(nullptr,
                                                                                {window.device()});
#endif
    if (studio && captureDir) {
#ifdef RECUBIN_SDL_GPU_STUDIO
        studioCaptureTarget.reset(createStudioCaptureTarget(window.device(), PREVIEW_WIDTH,
                                                            PREVIEW_HEIGHT, error));
        if (!studioCaptureTarget.get()) {
            std::cerr << "SDL scene preview: cannot create Studio capture target: "
                      << error << '\n';
            return 1;
        }
#else
        std::cerr << "SDL scene preview: Studio support is not compiled in\n";
        return 1;
#endif
    }

    bool running = true;
    unsigned layoutAttempts = 0;
    unsigned previousWidth = 0;
    unsigned previousHeight = 0;
    float orbitAngle = 0.0f;
    bool cameraDirty = true;
    std::cout << "SDL scene preview (" << window.driverName() << "): " << cubes.size()
              << " opaque Cube/MeshCube instances\n";
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            bool acceptCameraInput = !captureDir.has_value();
#ifdef RECUBIN_SDL_GPU_STUDIO
            if (studioUi) {
                studioUi->processEvent(event);
                acceptCameraInput = acceptCameraInput && !studioUi->wantsKeyboard();
            }
#endif
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
            if (acceptCameraInput && event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                    const float step = event.key.key == SDLK_LEFT ? -0.1f : 0.1f;
                    orbitAngle = std::remainder(orbitAngle + step, 2.0f * std::numbers::pi_v<float>);
                    cameraDirty = true;
                } else if (event.key.key == SDLK_HOME) {
                    orbitAngle = 0.0f;
                    cameraDirty = true;
                }
            }
        }
        if (!running) {
            break;
        }
        auto frame = window.beginFrame(&error);
        if (!frame) {
            std::cerr << "SDL scene preview: frame acquisition failed: " << error << '\n';
            return 1;
        }
        const unsigned width = frame.width != 0 ? frame.width : PREVIEW_WIDTH;
        const unsigned height = frame.height != 0 ? frame.height : PREVIEW_HEIGHT;
        if (config.present && !frame.swapchain) {
            if (!window.cancel(frame, &error)) {
                std::cerr << "SDL scene preview: cannot cancel unavailable swapchain frame: " << error << '\n';
                return 1;
            }
            SDL_Delay(16);
            continue;
        }
        if (cameraDirty || width != previousWidth || height != previousHeight) {
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const float distance = radius * std::max(1.0f, 1.0f / aspect);
            const float sine = std::sin(orbitAngle);
            const float cosine = std::cos(orbitAngle);
            const Vector3 eye = center +
                Vector3(1.35f * cosine + 1.65f * sine, 0.9f, 1.65f * cosine - 1.35f * sine) * distance;
            const Matrix4 view = Matrix4::LookAt(eye, center, Vector3(0, 1, 0));
            const Matrix4 projection = previewPerspective(aspect, std::max(0.01f, distance * 0.01f),
                                                          distance * 8.0f);
            const Matrix4 viewProjection = projection * view;
            for (std::size_t index = 0; index < cubes.size(); ++index) {
                const Matrix4 mvp = viewProjection * geometrySources[index].model;
                std::copy_n(mvp.m, 16, cubes[index].modelViewProjection.begin());
            }
            previousWidth = width;
            previousHeight = height;
            cameraDirty = false;
        }
        if (!renderer.draw(*frame.command, width, height, cubes, error)) {
            std::cerr << "SDL scene preview: draw failed: " << error << '\n';
            if (!window.submit(frame, nullptr, &error)) {
                std::cerr << "SDL scene preview: cannot retire failed frame: " << error << '\n';
            }
            return 1;
        }
        SDL_GPUTexture* resultTexture = renderer.colorTarget();
        bool captureReady = true;
#ifdef RECUBIN_SDL_GPU_STUDIO
        if (studioUi) {
            SDL_GPUTexture* destination = captureDir ? studioCaptureTarget.get() : frame.swapchain;
            if (!studioUi->draw(*frame.command, *destination, *resultTexture, error)) {
                std::cerr << "SDL scene preview: Studio UI draw failed: " << error << '\n';
                const bool retired = frame.swapchain ? window.submit(frame, nullptr, &error)
                                                    : window.cancel(frame, &error);
                if (!retired) {
                    std::cerr << "SDL scene preview: cannot retire failed UI frame: " << error << '\n';
                }
                return 1;
            }
            resultTexture = destination;
            captureReady = studioUi->hasSceneImage();
        } else
#endif
        if (frame.swapchain) {
            SDL_GPUBlitInfo blit{};
            blit.source = {resultTexture, 0, 0, 0, 0, width, height};
            blit.destination = {frame.swapchain, 0, 0, 0, 0, width, height};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(frame.command, &blit);
        }

        if (!captureDir || !captureReady) {
            if (!window.submit(frame, nullptr, &error)) {
                std::cerr << "SDL scene preview: submit failed: " << error << '\n';
                return 1;
            }
            if (captureDir && ++layoutAttempts == 8) {
                std::cerr << "SDL scene preview: ImGui did not emit the Scene image for capture\n";
                return 1;
            }
            continue;
        }
        SDL_GPUTransferBuffer* transfer = recordReadback(window.device(), frame.command,
                                                        resultTexture, width, height, error);
        if (!transfer) {
            std::cerr << "SDL scene preview: capture recording failed: " << error << '\n';
            if (!window.cancel(frame, &error)) {
                std::cerr << "SDL scene preview: cannot cancel capture frame: " << error << '\n';
            }
            return 1;
        }
        SDL_GPUFence* fence = nullptr;
        if (!window.submit(frame, &fence, &error)) {
            SDL_ReleaseGPUTransferBuffer(window.device(), transfer);
            std::cerr << "SDL scene preview: capture submission failed: " << error << '\n';
            return 1;
        }
        if (!finishReadback(window.device(), transfer, fence, width, height,
                            std::filesystem::u8path(*captureDir) / "render.png", error)) {
            std::cerr << "SDL scene preview: capture failed: " << error << '\n';
            return 1;
        }
        running = false;
    }
#ifdef RECUBIN_SDL_GPU_STUDIO
    // Destroy the UI resources while the GPU device is still alive. The
    // capture texture has a device-bound RAII deleter and is released first.
    studioCaptureTarget.reset();
    studioUi.reset();
#endif
    return 0;
}
