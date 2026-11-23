#pragma once

#include <Editor/ViewportSceneRenderer.hpp>

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SdlLiveScene;
class ViewportPanel;

// SDL GPU scene targets used by Studio viewports.  The renderer does not own
// the SDL device or command buffer; the Studio frame owns both of them.
class SdlStudioViewportRenderer final : public ViewportSceneRenderer {
public:
    explicit SdlStudioViewportRenderer(SDL_GPUDevice& device);
    ~SdlStudioViewportRenderer() override;

    SdlStudioViewportRenderer(const SdlStudioViewportRenderer&) = delete;
    SdlStudioViewportRenderer& operator=(const SdlStudioViewportRenderer&) = delete;

    void beginFrame(SDL_GPUCommandBuffer& commandBuffer, float elapsed);
    void endFrame();
    bool loadWelcomeLogo(ImTextureRef& image, int& width, int& height, std::string& error);

    ImTextureRef renderViewport(const ViewportPanel& panel,
                                const ViewportRenderDesc& request) override;
    void removeViewport(const ViewportPanel& panel) override;
    const std::string& error() const;

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUCommandBuffer* m_commandBuffer = nullptr;
    float m_elapsed = 0.0f;
    bool m_frameActive = false;
    std::string m_error;
    std::unordered_map<const ViewportPanel*, std::unique_ptr<SdlLiveScene>> m_scenes;
    std::vector<std::unique_ptr<SdlLiveScene>> m_retiredScenes;
    std::vector<SDL_GPUTexture*> m_ownedTextures;

    void reportError(const std::string& message);
};
