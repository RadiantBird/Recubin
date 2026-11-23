#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <string>

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
                std::string& error);
    void shutdown();

    bool isInitialized() const { return m_initialized; }

private:
    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
    ImGuiContext* m_context = nullptr;
    bool m_platformInitialized = false;
    bool m_rendererInitialized = false;
    bool m_initialized = false;
};
