#pragma once

#include <Editor/ViewportSceneRenderer.hpp>

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class SdlLiveScene;
class SdlGameGuiResources;
class GuiButton;
class ViewportPanel;

// SDL GPU scene targets used by Studio viewports.  The renderer does not own
// the SDL device or command buffer; the Studio frame owns both of them.
class SdlStudioViewportRenderer final : public ViewportSceneRenderer {
public:
    SdlStudioViewportRenderer(SDL_GPUDevice& device, ImFont* gameFont,
                              std::function<void(GuiButton*)> onButtonActivated);
    ~SdlStudioViewportRenderer() override;

    SdlStudioViewportRenderer(const SdlStudioViewportRenderer&) = delete;
    SdlStudioViewportRenderer& operator=(const SdlStudioViewportRenderer&) = delete;

    void beginFrame(SDL_GPUCommandBuffer& commandBuffer, float elapsed);
    void endFrame();
    bool loadWelcomeLogo(ImTextureRef& image, int& width, int& height, std::string& error);

    ImTextureRef renderViewport(const ViewportPanel& panel,
                                const ViewportRenderDesc& request) override;
    void renderGameGui(Workspace& workspace, User* user, const GameGuiRenderContext& context,
                       bool inputEnabled) override;
    void renderOutline(const ViewportPanel& panel, std::span<BaseCube* const> targets,
                       const Color4& color, float thickness) override;
    void renderTerrainGuide(const ViewportPanel& panel, const Vector3& center,
                            const Vector3& normal, float radius) override;
    void removeViewport(const ViewportPanel& panel) override;
    const std::string& error() const;

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUCommandBuffer* m_commandBuffer = nullptr;
    float m_elapsed = 0.0f;
    bool m_frameActive = false;
    std::string m_error;
    std::unordered_map<const ViewportPanel*, std::unique_ptr<SdlLiveScene>> m_scenes;
    std::unordered_set<const ViewportPanel*> m_renderedPanes;
    std::vector<std::unique_ptr<SdlLiveScene>> m_retiredScenes;
    std::vector<SDL_GPUTexture*> m_ownedTextures;
    std::unique_ptr<SdlGameGuiResources> m_gameGuiResources;
    std::function<void(GuiButton*)> m_onButtonActivated;

    void reportError(const std::string& message);
    SdlLiveScene* findOverlayScene(const ViewportPanel& panel);
};
