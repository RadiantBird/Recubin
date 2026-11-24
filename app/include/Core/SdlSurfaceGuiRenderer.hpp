#pragma once

#include <Core/SdlSurfaceGuiLayers.hpp>
#include <Core/GameGuiDrawing.hpp>
#include <Instances/SurfaceGui.hpp>
#include <SDL3/SDL_gpu.h>

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>

class SdlImGuiBackend;
class Workspace;
class GuiButton;
struct GameGuiRenderContext;

class SdlSurfaceGuiRenderer final {
public:
    explicit SdlSurfaceGuiRenderer(SDL_GPUDevice& device);
    ~SdlSurfaceGuiRenderer();
    SdlSurfaceGuiRenderer(const SdlSurfaceGuiRenderer&) = delete;
    SdlSurfaceGuiRenderer& operator=(const SdlSurfaceGuiRenderer&) = delete;

    bool bake(Workspace& workspace, SDL_GPUCommandBuffer& commandBuffer,
              SdlImGuiBackend& backend, const GameGuiDrawing::Resources& resources,
              std::string& error);
    GuiButton* clickedButton(Workspace& workspace, const GameGuiRenderContext& context) const;
    const SdlSurfaceGuiLayers& layers() const { return m_layers; }

private:
    struct Entry {
        std::weak_ptr<Instance> owner;
        SDL_GPUTexture* texture = nullptr;
        int width = 0;
        int height = 0;
        std::uint64_t signature = 0;
    };
    SDL_GPUDevice* m_device = nullptr;
    std::unordered_map<const SurfaceGui*, Entry> m_entries;
    SdlSurfaceGuiLayers m_layers;
};
