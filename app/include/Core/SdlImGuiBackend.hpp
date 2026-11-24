#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <string>
#include <string_view>
#include <filesystem>

struct ImDrawData;
struct ImGuiContext;

class SdlImGuiBackend final
{
public:
    SdlImGuiBackend() = default;
    ~SdlImGuiBackend();

    SdlImGuiBackend(const SdlImGuiBackend&) = delete;
    SdlImGuiBackend& operator=(const SdlImGuiBackend&) = delete;

    // The context, window, and device are borrowed and must outlive this backend.
    bool initialize(ImGuiContext& context,
                    SDL_Window& window,
                    SDL_GPUDevice& device,
                    SDL_GPUTextureFormat colorTargetFormat,
                    std::string& error);
    void processEvent(const SDL_Event& event);
    bool beginFrame(std::string& error);
    bool render(ImDrawData& drawData,
                SDL_GPUCommandBuffer& commandBuffer,
                SDL_GPUTexture& destination,
                std::string& error,
                bool loadDestination = false);
    bool renderPlatformWindows(std::string& error, std::string_view captureTitle = {},
                               const std::filesystem::path* capturePath = nullptr);
    bool ownsWindow(SDL_Window* window) const;
    void shutdown();

    bool isInitialized() const { return m_initialized; }

private:
    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
    ImGuiContext* m_context = nullptr;
    SDL_GPUTextureFormat m_colorTargetFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    std::string m_platformError;
    bool m_platformInitialized = false;
    bool m_rendererInitialized = false;
    bool m_initialized = false;

    static void createPlatformWindow(struct ImGuiViewport* viewport);
    static void destroyPlatformWindow(struct ImGuiViewport* viewport);
    static void renderPlatformWindow(struct ImGuiViewport* viewport, void* renderArg);
};
