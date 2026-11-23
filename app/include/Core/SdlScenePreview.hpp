#pragma once

#include <Core/SdlGpuWindow.hpp>

#include <Math/Matrix4.hpp>

#include <optional>
#include <filesystem>
#include <memory>
#include <string>
#include <span>

class User;
class Workspace;
class SdlBasicRenderer;
class BaseCube;

class SdlLiveScene final {
public:
    SdlLiveScene();
    ~SdlLiveScene();

    SdlLiveScene(const SdlLiveScene&) = delete;
    SdlLiveScene& operator=(const SdlLiveScene&) = delete;

    bool initialize(SDL_GPUDevice& device, std::string& error);
    bool sync(Workspace& workspace, std::string& error);
    bool draw(Workspace& workspace, const Matrix4& viewProjection,
              const Vector3& cameraPosition, const Vector3& cameraForward, float effectTime,
              SDL_GPUCommandBuffer& commandBuffer, unsigned width, unsigned height,
              std::string& error, std::span<BaseCube* const> selected = {});
    SDL_GPUTexture* colorTarget() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

Matrix4 sdlSceneViewProjection(const User& user, float aspect);
void blitSdlSceneFrame(SDL_GPUCommandBuffer& command, SDL_GPUTexture& source,
                       SDL_GPUTexture& destination, unsigned width, unsigned height);
bool captureSdlSceneFrame(SdlGpuWindow& window, SdlGpuWindow::Frame& frame,
                         SDL_GPUTexture* source, unsigned width, unsigned height,
                         const std::string& outputDir, std::string& error);
bool captureSdlFrameToPng(SdlGpuWindow& window, SdlGpuWindow::Frame& frame,
                          SDL_GPUTexture* source, unsigned width, unsigned height,
                          const std::filesystem::path& outputPath, std::string& error);

int runSdlScenePreview(const std::string& scenePath,
                       const std::string& title,
                       SdlGpuDriver driver,
                       const std::optional<std::string>& captureDir,
                       bool studio = false);
