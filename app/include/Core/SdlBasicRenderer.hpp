#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

struct SdlBasicVertex {
    std::array<float, 3> position{};
    std::array<float, 2> uv{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

struct SdlBasicCube {
    std::array<float, 16> modelViewProjection{};
    std::array<float, 16> shadowModelViewProjection{};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t geometry = 0;
    std::uint32_t texture = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    bool doubleSided = false;
    bool castShadow = true;
    bool receiveShadow = true;
    bool selected = false;
};

SDL_GPUTexture* uploadSdlRgbaTexture(SDL_GPUDevice& device, unsigned width, unsigned height,
                                     std::span<const std::uint8_t> rgba, std::string& error);

class SdlBasicRenderer final {
public:
    SdlBasicRenderer() = default;
    ~SdlBasicRenderer();

    SdlBasicRenderer(const SdlBasicRenderer&) = delete;
    SdlBasicRenderer& operator=(const SdlBasicRenderer&) = delete;

    bool initialize(SDL_GPUDevice& device, std::string& error);
    std::uint32_t uploadMesh(std::span<const SdlBasicVertex> vertices,
                             std::span<const std::uint32_t> indices,
                             std::string& error);
    bool releaseMesh(std::uint32_t handle, std::string& error);
    bool releaseTexture(std::uint32_t handle, std::string& error);
    std::uint32_t uploadTexture(unsigned width, unsigned height,
                                std::span<const std::uint8_t> rgba,
                                std::string& error);
    bool draw(SDL_GPUCommandBuffer& commandBuffer,
              unsigned width,
              unsigned height,
              std::span<const SdlBasicCube> cubes,
              std::string& error,
              bool shadows = false);

    [[nodiscard]] SDL_GPUTexture* colorTarget() const;

private:
    struct Geometry {
        SDL_GPUBuffer* vertexBuffer = nullptr;
        SDL_GPUBuffer* indexBuffer = nullptr;
        Uint32 indexCount = 0;
    };

    void releaseResources();
    bool ensureTargets(unsigned width, unsigned height, std::string& error);
    bool ensureShadowTarget(std::string& error);
    bool ensureSelectionTarget(unsigned width, unsigned height, std::string& error);

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShader* m_vertexShader = nullptr;
    SDL_GPUShader* m_fragmentShader = nullptr;
    SDL_GPUShader* m_shadowFragmentShader = nullptr;
    SDL_GPUShader* m_selectionMaskFragmentShader = nullptr;
    SDL_GPUShader* m_selectionOutlineVertexShader = nullptr;
    SDL_GPUShader* m_selectionOutlineFragmentShader = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
    SDL_GPUSampler* m_shadowSampler = nullptr;
    SDL_GPUSampler* m_selectionSampler = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_doubleSidedPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_shadowPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_selectionMaskPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_selectionMaskDoubleSidedPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_selectionOutlinePipeline = nullptr;
    SDL_GPUTexture* m_colorTarget = nullptr;
    SDL_GPUTexture* m_depthTarget = nullptr;
    SDL_GPUTexture* m_shadowTarget = nullptr;
    SDL_GPUTexture* m_selectionMaskTarget = nullptr;
    unsigned m_selectionWidth = 0;
    unsigned m_selectionHeight = 0;
    std::unordered_map<std::uint32_t, Geometry> m_geometries;
    std::uint32_t m_nextGeometryHandle = 1;
    std::unordered_map<std::uint32_t, SDL_GPUTexture*> m_textures;
    std::uint32_t m_nextTextureHandle = 1;
    SDL_GPUTexture* m_whiteTexture = nullptr;
    Geometry m_defaultGeometry{};
    unsigned m_width = 0;
    unsigned m_height = 0;
};
