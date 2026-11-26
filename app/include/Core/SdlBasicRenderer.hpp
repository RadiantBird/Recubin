#pragma once

#include <SDL3/SDL.h>
#include <Core/SdlMeshDecalData.hpp>
#include <Core/SdlMaterialLighting.hpp>
#include <Core/SdlCelestialDraw.hpp>
#include <Core/SdlSurfaceMarkDraw.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

struct Color4;
class BaseCube;

struct SdlBasicVertex {
    std::array<float, 3> position{};
    std::array<float, 2> uv{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    std::array<float, 3> normal{};
};

struct SdlBasicCube {
    const BaseCube* owner = nullptr;
    std::array<float, 16> modelViewProjection{};
    std::array<float, 16> shadowModelViewProjection{};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t materialBaseColor = 0;
    std::uint32_t materialRoughnessMap = 0;
    std::uint32_t materialMetallicMap = 0;
    std::uint32_t materialNormalMap = 0;
    float materialMetallic = 0.0f;
    float materialRoughness = 0.5f;
    float materialReflectance = 0.5f;
    float materialNormalStrength = 1.0f;
    bool materialPbr = false;
    bool unlit = false;
    float materialTileSize = 1.0f;
    std::array<float, 16> modelMatrix{};
    std::uint32_t geometry = 0;
    std::uint32_t texture = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    SDL_GPUTexture* surfaceTexture = nullptr;
    std::uint32_t faceTexture = 0;
    std::array<float, 4> faceTint{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 2> faceUvScale{1.0f, 1.0f};
    const SdlMeshDecalData* meshDecals = nullptr;
    bool blend = false;
    bool useTextureAlpha = true;
    bool liquid = false;
    float alphaThreshold = -1.0f;
    bool doubleSided = false;
    bool castShadow = true;
    bool receiveShadow = true;
};

struct SdlCloudDraw {
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::array<float, 16> model{};
    std::array<float, 4> color{};
    std::array<float, 2> windOffset{};
    float cover = 0.0f;
    float density = 0.0f;
    std::array<SdlBasicVertex, 6> vertices{};
};

struct SdlParticleVertex {
    std::array<float, 3> position{};
    std::array<float, 4> color{1, 1, 1, 1};
};

struct SdlSkyboxSource {
    std::array<float, 16> rotation{};
    std::array<float, 3> size{};
    std::uint64_t signature = 0;
};

SDL_GPUTexture* uploadSdlRgbaTexture(SDL_GPUDevice& device, unsigned width, unsigned height,
                                     std::span<const std::uint8_t> rgba, std::string& error);

class SdlBasicRenderer final {
public:
    // Geometry handle zero selects the renderer-owned 24-vertex cube.
    static constexpr std::uint32_t DEFAULT_GEOMETRY_HANDLE = 0;

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
    // When supplying sky faces, place blended draws after all opaque draws.
    bool draw(SDL_GPUCommandBuffer& commandBuffer,
              unsigned width,
              unsigned height,
              std::span<const SdlBasicCube> cubes,
              std::string& error,
              bool shadows = false,
              const SdlMaterialLighting& lighting = {},
              std::span<const SdlBasicCube> skyFaces = {},
              const SdlSkyboxSource* skyboxSource = nullptr,
              std::span<const SdlCelestialDraw> celestial = {},
              std::span<const SdlCloudDraw> clouds = {},
              std::span<const SdlParticleVertex> particles = {},
              const std::array<float, 16>* particleView = nullptr,
              const std::array<float, 16>* particleProjection = nullptr,
              float effectTime = 0.0f,
              std::span<const SdlSurfaceMarkDraw> surfaceMarks = {});
    bool drawOutline(SDL_GPUCommandBuffer& commandBuffer, SDL_GPUTexture& destination,
                     std::span<const SdlBasicCube> draws, const Color4& color, float thickness,
                     std::string& error, float effectTime = 0.0f);
    bool drawTerrainGuide(SDL_GPUCommandBuffer& commandBuffer, SDL_GPUTexture& destination,
                          const std::array<float, 16>& viewProjectionFromGuide, float radius,
                          std::string& error);

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
    SDL_GPUTexture* resolveTexture(std::uint32_t handle, std::string& error) const;
    bool drawMesh(SDL_GPURenderPass& pass, const SdlBasicCube& cube, std::string& error) const;

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShader* m_vertexShader = nullptr;
    SDL_GPUShader* m_fragmentShader = nullptr;
    SDL_GPUShader* m_shadowFragmentShader = nullptr;
    SDL_GPUShader* m_selectionMaskFragmentShader = nullptr;
    SDL_GPUShader* m_selectionOutlineVertexShader = nullptr;
    SDL_GPUShader* m_selectionOutlineFragmentShader = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
    SDL_GPUSampler* m_skySampler = nullptr;
    SDL_GPUSampler* m_shadowSampler = nullptr;
    SDL_GPUSampler* m_selectionSampler = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_doubleSidedPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_blendPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_blendDoubleSidedPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_skyPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_shadowPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_selectionMaskPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_selectionMaskDoubleSidedPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_selectionOutlinePipeline = nullptr;
    SDL_GPUShader* m_terrainGuideVertexShader = nullptr;
    SDL_GPUShader* m_terrainGuideFragmentShader = nullptr;
    SDL_GPUShader* m_celestialVertexShader = nullptr;
    SDL_GPUShader* m_celestialFragmentShader = nullptr;
    SDL_GPUShader* m_cloudVertexShader = nullptr;
    SDL_GPUShader* m_cloudFragmentShader = nullptr;
    SDL_GPUShader* m_environmentPrefilterVertexShader = nullptr;
    SDL_GPUShader* m_environmentPrefilterFragmentShader = nullptr;
    SDL_GPUShader* m_surfaceMarkVertexShader = nullptr;
    SDL_GPUShader* m_surfaceMarkFragmentShader = nullptr;
    SDL_GPUShader* m_surfaceMarkDepthFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* m_terrainGuidePipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_celestialDiscPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_celestialAdditivePipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_cloudPipeline = nullptr;
    SDL_GPUShader* m_particleVertexShader = nullptr;
    SDL_GPUShader* m_particleFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* m_particlePipeline = nullptr;
    SDL_GPUBuffer* m_particleVertexBuffer = nullptr;
    Uint32 m_particleVertexCapacity = 0;
    SDL_GPUGraphicsPipeline* m_environmentPrefilterPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_surfaceMarkDepthPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_surfaceMarkPipeline = nullptr;
    SDL_GPUSampler* m_cloudSampler = nullptr;
    SDL_GPUSampler* m_environmentPrefilterSampler = nullptr;
    SDL_GPUSampler* m_surfaceMarkSampler = nullptr;
    SDL_GPUSampler* m_surfaceMarkDepthSampler = nullptr;
    SDL_GPUTexture* m_cloudNoiseTexture = nullptr;
    SDL_GPUTexture* m_colorTarget = nullptr;
    SDL_GPUTexture* m_depthTarget = nullptr;
    SDL_GPUTexture* m_shadowTarget = nullptr;
    SDL_GPUTexture* m_environmentFallbackCubemap = nullptr;
    SDL_GPUTexture* m_skyboxSourceCubemap = nullptr;
    SDL_GPUTexture* m_skyboxSourceDepth = nullptr;
    SDL_GPUTexture* m_skyboxPrefilteredCubemap = nullptr;
    SDL_GPUTexture* m_surfaceMarkDepthTarget = nullptr;
    std::uint64_t m_skyboxSourceSignature = 0;
    std::uint64_t m_skyboxEnvironmentFailureSignature = 0;
    SDL_GPUTexture* m_selectionMaskTarget = nullptr;
    unsigned m_selectionWidth = 0;
    unsigned m_selectionHeight = 0;
    std::unordered_map<std::uint32_t, Geometry> m_geometries;
    std::uint32_t m_nextGeometryHandle = 1;
    std::unordered_map<std::uint32_t, SDL_GPUTexture*> m_textures;
    std::uint32_t m_nextTextureHandle = 1;
    SDL_GPUTexture* m_whiteTexture = nullptr;
    Geometry m_defaultGeometry{};
    Geometry m_cloudGeometry{};
    unsigned m_width = 0;
    unsigned m_height = 0;
};
