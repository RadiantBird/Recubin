#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

class Workspace;

class SdlPostEffects final {
public:
    SdlPostEffects();
    ~SdlPostEffects();

    SdlPostEffects(const SdlPostEffects&) = delete;
    SdlPostEffects& operator=(const SdlPostEffects&) = delete;

    bool initialize(SDL_GPUDevice& device, std::string& error);
    bool apply(Workspace& workspace, SDL_GPUCommandBuffer& commandBuffer,
               SDL_GPUTexture& input, unsigned width, unsigned height,
               float elapsed, SDL_GPUTexture*& output, std::string& error);

private:
    struct EffectState;

    void releaseResources();
    bool ensureTargets(unsigned width, unsigned height, std::string& error);
    bool refreshEffect(EffectState& state, const std::string& path, std::string& error);

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShader* m_vertexShader = nullptr;
    SDL_GPUShader* m_builtinFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* m_builtinPipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
    SDL_GPUTexture* m_pingPong[2] = {nullptr, nullptr};
    unsigned m_width = 0;
    unsigned m_height = 0;
    std::unordered_map<std::string, std::unique_ptr<EffectState>> m_effects;
};
