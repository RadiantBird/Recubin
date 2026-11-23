#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "Core/SdlImGuiBackend.hpp"

#include <cstdint>
#include <string>

struct ImGuiContext;

class SdlPreviewImGui final
{
public:
    SdlPreviewImGui() = default;
    ~SdlPreviewImGui();

    SdlPreviewImGui(const SdlPreviewImGui&) = delete;
    SdlPreviewImGui& operator=(const SdlPreviewImGui&) = delete;

    bool initialize(SDL_Window& window,
                    SDL_GPUDevice& device,
                    SDL_GPUTextureFormat colorTargetFormat,
                    std::string& error);
    void processEvent(const SDL_Event& event);
    bool wantsKeyboard() const { return m_wantsKeyboard; }
    bool hasSceneImage() const { return m_hasSceneImage; }
    bool draw(SDL_GPUCommandBuffer& commandBuffer,
              SDL_GPUTexture& destination,
              SDL_GPUTexture& scene,
              std::string& error);

private:
    void cleanup();

    ImGuiContext* m_context = nullptr;
    ImGuiContext* m_previousContext = nullptr;
    SdlImGuiBackend m_backend;
    bool m_wantsKeyboard = false;
    bool m_hasSceneImage = false;
};
