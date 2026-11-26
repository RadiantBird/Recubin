#include "Core/SdlBasicRenderer.hpp"
#include <Util/Color4.hpp>
#include <algorithm>
#include <array>
#include <basic_gpu_shaders.hpp>
#include <selection_mask_gpu_shaders.hpp>
#include <selection_outline_gpu_shaders.hpp>
#include <post_effect_gpu_shaders.hpp>
#include <terrain_guide_gpu_shaders.hpp>
#include <shadow_gpu_shaders.hpp>
#include <celestial_gpu_shaders.hpp>
#include <cloud_gpu_shaders.hpp>
#include <particle_gpu_shaders.hpp>
#include <env_prefilter_gpu_shaders.hpp>
#include <surface_mark_gpu_shaders.hpp>
#include <surface_mark_depth_gpu_shaders.hpp>
#include <Math/PerlinNoise.hpp>
#include <Math/Matrix4.hpp>
#include <Instances/Cube.hpp>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>

namespace {
constexpr SDL_GPUTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr unsigned SHADOW_MAP_SIZE = 2048;
constexpr unsigned ENVIRONMENT_FALLBACK_SIZE = 32;
constexpr unsigned ENVIRONMENT_FALLBACK_MIPS = 6;
constexpr unsigned SKYBOX_SOURCE_SIZE = 256;
constexpr unsigned ENVIRONMENT_PREFILTER_SIZE = 128;
constexpr unsigned ENVIRONMENT_PREFILTER_MIPS = 6;
constexpr unsigned SURFACE_MARK_MAP_SIZE = 1024;
constexpr Uint32 TERRAIN_GUIDE_VERTEX_COUNT = 2 * (48 + 1); // terrain_guide.hlsl
struct SdlMaterialLightingGpu {
    std::array<float, 4> cameraAndLightCount{};
    std::array<float, 4> sunDirection{};
    std::array<float, 4> sunIrradiance{};
    std::array<SdlLocalLight, 8> lights{};
    std::array<float, 4> environmentParams{};
};
static_assert(offsetof(SdlMaterialLightingGpu, environmentParams) == 432);
static_assert(sizeof(SdlMaterialLightingGpu) == 448);
std::string sdlError(std::string_view operation) {
    std::string message(operation);
    const char* detail = SDL_GetError();
    if (detail != nullptr && detail[0] != '\0') {
        message += ": ";
        message += detail;
    }
    return message;
}
bool makeFaceCubeVertices(std::array<SdlBasicVertex, 24>& vertices, std::string& error) {
    const auto source = createCubeVertices(1.0f);
    if (source.size() != vertices.size()) {
        error = "SdlBasicRenderer: createCubeVertices returned " +
                std::to_string(source.size()) + " vertices; expected 24";
        return false;
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position = {source[i].Position.x, source[i].Position.y, source[i].Position.z};
        vertices[i].uv = {source[i].U, 1.0f - source[i].V};
        vertices[i].normal = {source[i].Normal.x, source[i].Normal.y, source[i].Normal.z};
    }
    return true;
}
constexpr std::array<std::uint32_t, 36> FACE_CUBE_INDICES = {{0, 2, 1, 0, 3, 2,
    4, 6, 5, 4, 7, 6, 8, 10, 9, 8, 11, 10, 12, 14, 13, 12, 15, 14,
    16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22}};

std::array<float, 3> fallbackFaceDirection(unsigned face, float u, float v) {
    switch (face) {
    case 0: return {1.0f, -v, -u};
    case 1: return {-1.0f, -v, u};
    case 2: return {u, 1.0f, v};
    case 3: return {u, -1.0f, -v};
    case 4: return {u, -v, 1.0f};
    default: return {-u, -v, -1.0f};
    }
}

std::array<float, 3> fallbackSkyGradient(float y) {
    constexpr std::array<float, 3> horizon{0.80f, 0.84f, 0.90f};
    constexpr std::array<float, 3> zenith{0.35f, 0.55f, 0.88f};
    constexpr std::array<float, 3> ground{0.34f, 0.32f, 0.30f};
    std::array<float, 3> color{};
    if (y >= 0.0f) {
        const float blend = std::sqrt(y);
        for (unsigned channel = 0; channel < 3; ++channel)
            color[channel] = horizon[channel] + (zenith[channel] - horizon[channel]) * blend;
    } else {
        const float t = std::min(-y / 0.35f, 1.0f);
        const float blend = t * t * (3.0f - 2.0f * t);
        for (unsigned channel = 0; channel < 3; ++channel)
            color[channel] = horizon[channel] + (ground[channel] - horizon[channel]) * blend;
    }
    return color;
}

SDL_GPUTexture* createFallbackCubemap(SDL_GPUDevice& device, std::string& error) {
    constexpr std::size_t bytesPerPixel = 4;
    std::size_t totalBytes = 0;
    for (unsigned mip = 0; mip < ENVIRONMENT_FALLBACK_MIPS; ++mip) {
        const unsigned size = std::max(1u, ENVIRONMENT_FALLBACK_SIZE >> mip);
        totalBytes += static_cast<std::size_t>(size) * size * bytesPerPixel * 6;
    }
    if (totalBytes > std::numeric_limits<Uint32>::max()) {
        error = "SDL environment fallback cubemap exceeds transfer buffer size";
        return nullptr;
    }
    std::vector<std::uint8_t> pixels(totalBytes);
    auto mipOffset = [](unsigned mip) {
        std::size_t result = 0;
        for (unsigned previous = 0; previous < mip; ++previous) {
            const unsigned previousSize = std::max(1u, ENVIRONMENT_FALLBACK_SIZE >> previous);
            result += static_cast<std::size_t>(previousSize) * previousSize * bytesPerPixel * 6;
        }
        return result;
    };
    const unsigned baseSize = ENVIRONMENT_FALLBACK_SIZE;
    for (unsigned face = 0; face < 6; ++face) {
        for (unsigned row = 0; row < baseSize; ++row) {
            for (unsigned column = 0; column < baseSize; ++column) {
                const float u = (static_cast<float>(column) + 0.5f) / baseSize * 2.0f - 1.0f;
                const float v = (static_cast<float>(row) + 0.5f) / baseSize * 2.0f - 1.0f;
                const auto direction = fallbackFaceDirection(face, u, v);
                const float length = std::sqrt(direction[0] * direction[0] +
                                               direction[1] * direction[1] +
                                               direction[2] * direction[2]);
                const auto color = fallbackSkyGradient(direction[1] / length);
                const std::size_t texel =
                    (static_cast<std::size_t>(face) * baseSize * baseSize +
                     static_cast<std::size_t>(row) * baseSize + column) * bytesPerPixel;
                for (unsigned channel = 0; channel < 3; ++channel) {
                    pixels[texel + channel] = static_cast<std::uint8_t>(
                        std::lround(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f));
                }
                pixels[texel + 3] = 255;
            }
        }
    }
    for (unsigned mip = 1; mip < ENVIRONMENT_FALLBACK_MIPS; ++mip) {
        const unsigned size = std::max(1u, ENVIRONMENT_FALLBACK_SIZE >> mip);
        const unsigned previousSize = size * 2;
        const std::size_t destinationOffset = mipOffset(mip);
        const std::size_t sourceOffset = mipOffset(mip - 1);
        for (unsigned face = 0; face < 6; ++face) {
            for (unsigned row = 0; row < size; ++row) {
                for (unsigned column = 0; column < size; ++column) {
                    const std::size_t destination = destinationOffset +
                        (static_cast<std::size_t>(face) * size * size +
                         static_cast<std::size_t>(row) * size + column) * bytesPerPixel;
                    const std::size_t source = sourceOffset +
                        (static_cast<std::size_t>(face) * previousSize * previousSize +
                         static_cast<std::size_t>(row * 2) * previousSize + column * 2) * bytesPerPixel;
                    for (unsigned channel = 0; channel < 4; ++channel) {
                        const unsigned sum = pixels[source + channel] +
                            pixels[source + bytesPerPixel + channel] +
                            pixels[source + previousSize * bytesPerPixel + channel] +
                            pixels[source + (previousSize + 1) * bytesPerPixel + channel];
                        pixels[destination + channel] = static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                }
            }
        }
    }
    std::size_t offset = 0;
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
    textureInfo.format = COLOR_FORMAT;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = ENVIRONMENT_FALLBACK_SIZE;
    textureInfo.height = ENVIRONMENT_FALLBACK_SIZE;
    textureInfo.layer_count_or_depth = 6;
    textureInfo.num_levels = ENVIRONMENT_FALLBACK_MIPS;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(&device, &textureInfo);
    if (!texture) {
        error = sdlError("SDL_CreateGPUTexture(environment fallback cubemap)");
        return nullptr;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(totalBytes);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(&device, &transferInfo);
    if (!transfer) {
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_CreateGPUTransferBuffer(environment fallback cubemap)");
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(&device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(&device, transfer);
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_MapGPUTransferBuffer(environment fallback cubemap)");
        return nullptr;
    }
    std::memcpy(mapped, pixels.data(), totalBytes);
    SDL_UnmapGPUTransferBuffer(&device, transfer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(&device);
    if (!command) {
        SDL_ReleaseGPUTransferBuffer(&device, transfer);
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_AcquireGPUCommandBuffer(environment fallback cubemap)");
        return nullptr;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(&device, transfer);
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_BeginGPUCopyPass(environment fallback cubemap)");
        return nullptr;
    }
    offset = 0;
    for (unsigned mip = 0; mip < ENVIRONMENT_FALLBACK_MIPS; ++mip) {
        const unsigned size = std::max(1u, ENVIRONMENT_FALLBACK_SIZE >> mip);
        for (unsigned face = 0; face < 6; ++face) {
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            source.offset = static_cast<Uint32>(offset);
            source.pixels_per_row = size;
            source.rows_per_layer = size;
            SDL_GPUTextureRegion destination{texture, mip, face, 0, 0, 0, size, size, 1};
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
            offset += static_cast<std::size_t>(size) * size * bytesPerPixel;
        }
    }
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    const bool uploaded = fence != nullptr && SDL_WaitForGPUFences(&device, true, &fence, 1);
    if (fence) SDL_ReleaseGPUFence(&device, fence);
    SDL_ReleaseGPUTransferBuffer(&device, transfer);
    if (!uploaded) {
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_SubmitGPUCommandBufferAndAcquireFence(environment fallback cubemap)");
        return nullptr;
    }
    return texture;
}

SDL_GPUTexture* createSkyboxTarget(SDL_GPUDevice& device, SDL_GPUTextureUsageFlags usage,
                                   SDL_GPUTextureFormat format, std::string& error) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_CUBE;
    info.format = format;
    info.usage = usage;
    info.width = SKYBOX_SOURCE_SIZE;
    info.height = SKYBOX_SOURCE_SIZE;
    info.layer_count_or_depth = 6;
    info.num_levels = usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET ? 9 : 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(&device, &info);
    if (!texture) error = sdlError("SDL_CreateGPUTexture(skybox source)");
    return texture;
}

SDL_GPUTexture* createEnvironmentPrefilterTarget(SDL_GPUDevice& device, std::string& error) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_CUBE;
    info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = ENVIRONMENT_PREFILTER_SIZE;
    info.height = ENVIRONMENT_PREFILTER_SIZE;
    info.layer_count_or_depth = 6;
    info.num_levels = ENVIRONMENT_PREFILTER_MIPS;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(&device, &info);
    if (!texture) error = sdlError("SDL_CreateGPUTexture(environment prefilter cubemap)");
    return texture;
}
} // namespace

SDL_GPUTexture* uploadSdlRgbaTexture(SDL_GPUDevice& device, unsigned width, unsigned height,
                                     std::span<const std::uint8_t> rgba, std::string& error) {
    error.clear();
    if (width == 0 || height == 0) {
        error = "SDL texture dimensions must be non-zero";
        return nullptr;
    }
    constexpr std::size_t bytesPerPixel = 4;
    if (width > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
        height > std::numeric_limits<std::size_t>::max() / (width * bytesPerPixel)) {
        error = "SDL texture dimensions overflow";
        return nullptr;
    }
    const std::size_t bytes = static_cast<std::size_t>(width) * height * bytesPerPixel;
    if (rgba.size() != bytes || bytes > std::numeric_limits<Uint32>::max()) {
        error = "RGBA texture data has an unexpected size";
        return nullptr;
    }
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = COLOR_FORMAT;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(&device, &textureInfo);
    if (!texture) {
        error = sdlError("SDL_CreateGPUTexture(texture)");
        return nullptr;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(bytes);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(&device, &transferInfo);
    if (!transfer) {
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_CreateGPUTransferBuffer(texture)");
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(&device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(&device, transfer);
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_MapGPUTransferBuffer(texture)");
        return nullptr;
    }
    std::memcpy(mapped, rgba.data(), bytes);
    SDL_UnmapGPUTransferBuffer(&device, transfer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(&device);
    if (!command) {
        SDL_ReleaseGPUTransferBuffer(&device, transfer);
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_AcquireGPUCommandBuffer(texture)");
        return nullptr;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(&device, transfer);
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_BeginGPUCopyPass(texture)");
        return nullptr;
    }
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_GPUTextureRegion destination{texture, 0, 0, 0, 0, 0, width, height, 1};
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    const bool uploaded = fence != nullptr && SDL_WaitForGPUFences(&device, true, &fence, 1);
    if (fence) SDL_ReleaseGPUFence(&device, fence);
    SDL_ReleaseGPUTransferBuffer(&device, transfer);
    if (!uploaded) {
        SDL_ReleaseGPUTexture(&device, texture);
        error = sdlError("SDL_SubmitGPUCommandBufferAndAcquireFence(texture)");
        return nullptr;
    }
    return texture;
}

SdlBasicRenderer::~SdlBasicRenderer() { releaseResources(); }
void SdlBasicRenderer::releaseResources() {
    if (m_device != nullptr) {
        SDL_WaitForGPUIdle(m_device);
    }
    auto release = [this](Geometry& g) {
        if (g.vertexBuffer) {
            SDL_ReleaseGPUBuffer(m_device, g.vertexBuffer);
        }
        if (g.indexBuffer) {
            SDL_ReleaseGPUBuffer(m_device, g.indexBuffer);
        }
        g = {};
    };
    release(m_defaultGeometry);
    release(m_cloudGeometry);
    for (auto& [handle, geometry] : m_geometries) {
        release(geometry);
    }
    m_geometries.clear();
    m_nextGeometryHandle = 1;
    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
    }
    if (m_doubleSidedPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_doubleSidedPipeline);
    }
    if (m_blendPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_blendPipeline);
    }
    if (m_blendDoubleSidedPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_blendDoubleSidedPipeline);
    }
    if (m_skyPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_skyPipeline);
    }
    if (m_shadowPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_shadowPipeline);
    }
    if (m_selectionMaskPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_selectionMaskPipeline);
    }
    if (m_selectionMaskDoubleSidedPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_selectionMaskDoubleSidedPipeline);
    }
    if (m_selectionOutlinePipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_selectionOutlinePipeline);
    }
    if (m_terrainGuidePipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_terrainGuidePipeline);
    if (m_celestialDiscPipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_celestialDiscPipeline);
    if (m_celestialAdditivePipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_celestialAdditivePipeline);
    if (m_cloudPipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_cloudPipeline);
    if (m_particlePipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_particlePipeline);
    if (m_environmentPrefilterPipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_environmentPrefilterPipeline);
    if (m_surfaceMarkDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_surfaceMarkDepthPipeline);
    if (m_surfaceMarkPipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_surfaceMarkPipeline);
    if (m_vertexShader) {
        SDL_ReleaseGPUShader(m_device, m_vertexShader);
    }
    if (m_fragmentShader) {
        SDL_ReleaseGPUShader(m_device, m_fragmentShader);
    }
    if (m_shadowFragmentShader) {
        SDL_ReleaseGPUShader(m_device, m_shadowFragmentShader);
    }
    if (m_selectionMaskFragmentShader) {
        SDL_ReleaseGPUShader(m_device, m_selectionMaskFragmentShader);
    }
    if (m_selectionOutlineVertexShader) {
        SDL_ReleaseGPUShader(m_device, m_selectionOutlineVertexShader);
    }
    if (m_selectionOutlineFragmentShader) {
        SDL_ReleaseGPUShader(m_device, m_selectionOutlineFragmentShader);
    }
    if (m_terrainGuideVertexShader) SDL_ReleaseGPUShader(m_device, m_terrainGuideVertexShader);
    if (m_terrainGuideFragmentShader) SDL_ReleaseGPUShader(m_device, m_terrainGuideFragmentShader);
    if (m_celestialVertexShader) SDL_ReleaseGPUShader(m_device, m_celestialVertexShader);
    if (m_celestialFragmentShader) SDL_ReleaseGPUShader(m_device, m_celestialFragmentShader);
    if (m_cloudVertexShader) SDL_ReleaseGPUShader(m_device, m_cloudVertexShader);
    if (m_cloudFragmentShader) SDL_ReleaseGPUShader(m_device, m_cloudFragmentShader);
    if (m_particleVertexShader) SDL_ReleaseGPUShader(m_device, m_particleVertexShader);
    if (m_particleFragmentShader) SDL_ReleaseGPUShader(m_device, m_particleFragmentShader);
    if (m_environmentPrefilterVertexShader) SDL_ReleaseGPUShader(m_device, m_environmentPrefilterVertexShader);
    if (m_environmentPrefilterFragmentShader) SDL_ReleaseGPUShader(m_device, m_environmentPrefilterFragmentShader);
    if (m_surfaceMarkVertexShader) SDL_ReleaseGPUShader(m_device, m_surfaceMarkVertexShader);
    if (m_surfaceMarkFragmentShader) SDL_ReleaseGPUShader(m_device, m_surfaceMarkFragmentShader);
    if (m_surfaceMarkDepthFragmentShader) SDL_ReleaseGPUShader(m_device, m_surfaceMarkDepthFragmentShader);
    if (m_particleVertexBuffer) SDL_ReleaseGPUBuffer(m_device, m_particleVertexBuffer);
    if (m_sampler) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
    }
    if (m_skySampler) {
        SDL_ReleaseGPUSampler(m_device, m_skySampler);
    }
    if (m_shadowSampler) {
        SDL_ReleaseGPUSampler(m_device, m_shadowSampler);
    }
    if (m_selectionSampler) {
        SDL_ReleaseGPUSampler(m_device, m_selectionSampler);
    }
    if (m_cloudSampler) SDL_ReleaseGPUSampler(m_device, m_cloudSampler);
    if (m_environmentPrefilterSampler) SDL_ReleaseGPUSampler(m_device, m_environmentPrefilterSampler);
    if (m_surfaceMarkSampler) SDL_ReleaseGPUSampler(m_device, m_surfaceMarkSampler);
    if (m_surfaceMarkDepthSampler) SDL_ReleaseGPUSampler(m_device, m_surfaceMarkDepthSampler);
    if (m_colorTarget) {
        SDL_ReleaseGPUTexture(m_device, m_colorTarget);
    }
    if (m_depthTarget) {
        SDL_ReleaseGPUTexture(m_device, m_depthTarget);
    }
    if (m_shadowTarget) {
        SDL_ReleaseGPUTexture(m_device, m_shadowTarget);
    }
    if (m_cloudNoiseTexture) SDL_ReleaseGPUTexture(m_device, m_cloudNoiseTexture);
    if (m_environmentFallbackCubemap) SDL_ReleaseGPUTexture(m_device, m_environmentFallbackCubemap);
    if (m_skyboxSourceCubemap) SDL_ReleaseGPUTexture(m_device, m_skyboxSourceCubemap);
    if (m_skyboxSourceDepth) SDL_ReleaseGPUTexture(m_device, m_skyboxSourceDepth);
    if (m_skyboxPrefilteredCubemap) SDL_ReleaseGPUTexture(m_device, m_skyboxPrefilteredCubemap);
    if (m_surfaceMarkDepthTarget) SDL_ReleaseGPUTexture(m_device, m_surfaceMarkDepthTarget);
    if (m_selectionMaskTarget) {
        SDL_ReleaseGPUTexture(m_device, m_selectionMaskTarget);
    }
    for (const auto& [handle, texture] : m_textures) {
        if (texture) {
            SDL_ReleaseGPUTexture(m_device, texture);
        }
    }
    m_textures.clear();
    m_nextTextureHandle = 1;
    m_pipeline = nullptr;
    m_doubleSidedPipeline = nullptr;
    m_blendPipeline = nullptr;
    m_blendDoubleSidedPipeline = nullptr;
    m_skyPipeline = nullptr;
    m_shadowPipeline = nullptr;
    m_selectionMaskPipeline = nullptr;
    m_selectionMaskDoubleSidedPipeline = nullptr;
    m_selectionOutlinePipeline = nullptr;
    m_terrainGuidePipeline = nullptr;
    m_celestialDiscPipeline = nullptr;
    m_celestialAdditivePipeline = nullptr;
    m_cloudPipeline = nullptr;
    m_particlePipeline = nullptr;
    m_environmentPrefilterPipeline = nullptr;
    m_surfaceMarkDepthPipeline = nullptr;
    m_surfaceMarkPipeline = nullptr;
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_shadowFragmentShader = nullptr;
    m_selectionMaskFragmentShader = nullptr;
    m_selectionOutlineVertexShader = nullptr;
    m_selectionOutlineFragmentShader = nullptr;
    m_terrainGuideVertexShader = nullptr;
    m_terrainGuideFragmentShader = nullptr;
    m_celestialVertexShader = nullptr;
    m_celestialFragmentShader = nullptr;
    m_cloudVertexShader = nullptr;
    m_cloudFragmentShader = nullptr;
    m_particleVertexShader = nullptr;
    m_particleFragmentShader = nullptr;
    m_environmentPrefilterVertexShader = nullptr;
    m_environmentPrefilterFragmentShader = nullptr;
    m_surfaceMarkVertexShader = nullptr;
    m_surfaceMarkFragmentShader = nullptr;
    m_surfaceMarkDepthFragmentShader = nullptr;
    m_sampler = nullptr;
    m_skySampler = nullptr;
    m_shadowSampler = nullptr;
    m_selectionSampler = nullptr;
    m_cloudSampler = nullptr;
    m_particleVertexBuffer = nullptr;
    m_particleVertexCapacity = 0;
    m_environmentPrefilterSampler = nullptr;
    m_surfaceMarkSampler = nullptr;
    m_surfaceMarkDepthSampler = nullptr;
    m_whiteTexture = nullptr;
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
    m_shadowTarget = nullptr;
    m_cloudNoiseTexture = nullptr;
    m_environmentFallbackCubemap = nullptr;
    m_skyboxSourceCubemap = nullptr;
    m_skyboxSourceDepth = nullptr;
    m_skyboxPrefilteredCubemap = nullptr;
    m_surfaceMarkDepthTarget = nullptr;
    m_skyboxSourceSignature = 0;
    m_skyboxEnvironmentFailureSignature = 0;
    m_selectionMaskTarget = nullptr;
    m_selectionWidth = 0;
    m_selectionHeight = 0;
    m_width = 0;
    m_height = 0;
}

bool SdlBasicRenderer::initialize(SDL_GPUDevice& device, std::string& error) {
    error.clear();
    releaseResources();
    m_device = &device;
    const char* driver = SDL_GetGPUDeviceDriver(&device);
    if (!driver) {
        error = sdlError("SDL_GetGPUDeviceDriver");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    const Uint8* vc = nullptr;
    size_t vs = 0;
    const Uint8* fc = nullptr;
    size_t fs = 0;
    const bool metal = std::string_view(driver) == "metal";
    if (std::string_view(driver) == "vulkan") {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        vc = basic_gpu_vertex_spirv;
        vs = sizeof(basic_gpu_vertex_spirv);
        fc = basic_gpu_fragment_spirv;
        fs = sizeof(basic_gpu_fragment_spirv);
    } else if (std::string_view(driver) == "direct3d12") {
        format = SDL_GPU_SHADERFORMAT_DXIL;
        vc = basic_gpu_vertex_dxil;
        vs = sizeof(basic_gpu_vertex_dxil);
        fc = basic_gpu_fragment_dxil;
        fs = sizeof(basic_gpu_fragment_dxil);
    } else if (metal) {
        format = SDL_GPU_SHADERFORMAT_MSL;
        vc = basic_gpu_vertex_msl;
        vs = sizeof(basic_gpu_vertex_msl);
        fc = basic_gpu_fragment_msl;
        fs = sizeof(basic_gpu_fragment_msl);
    } else {
        error = "SdlBasicRenderer: unsupported SDL GPU driver '" + std::string(driver) + "'";
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo vi{};
    vi.code = vc;
    vi.code_size = vs;
    vi.entrypoint = "VSMain";
    vi.format = format;
    vi.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vi.num_uniform_buffers = 1;
    m_vertexShader = SDL_CreateGPUShader(&device, &vi);
    if (!m_vertexShader) {
        error = sdlError("SDL_CreateGPUShader(VSMain)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo fi{};
    fi.code = fc;
    fi.code_size = fs;
    fi.entrypoint = "PSMain";
    fi.format = format;
    fi.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fi.num_samplers = 9;
    fi.num_uniform_buffers = 2;
    m_fragmentShader = SDL_CreateGPUShader(&device, &fi);
    if (!m_fragmentShader) {
        error = sdlError("SDL_CreateGPUShader(PSMain)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = sizeof(SdlBasicVertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute va{};
    va.location = 0;
    va.buffer_slot = 0;
    va.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    va.offset = offsetof(SdlBasicVertex, position);
    SDL_GPUVertexAttribute uvAttribute{};
    uvAttribute.location = 1;
    uvAttribute.buffer_slot = 0;
    uvAttribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    uvAttribute.offset = offsetof(SdlBasicVertex, uv);
    SDL_GPUVertexAttribute colorAttribute{};
    colorAttribute.location = 2;
    colorAttribute.buffer_slot = 0;
    colorAttribute.format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    colorAttribute.offset = offsetof(SdlBasicVertex, color);
    SDL_GPUVertexAttribute normalAttribute{};
    normalAttribute.location = 3;
    normalAttribute.buffer_slot = 0;
    normalAttribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    normalAttribute.offset = offsetof(SdlBasicVertex, normal);
    SDL_GPUColorTargetDescription ct{};
    ct.format = COLOR_FORMAT;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = m_vertexShader;
    pi.fragment_shader = m_fragmentShader;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb;
    pi.vertex_input_state.num_vertex_buffers = 1;
    const std::array<SDL_GPUVertexAttribute, 4> attributes = {va, uvAttribute, colorAttribute,
                                                               normalAttribute};
    pi.vertex_input_state.vertex_attributes = attributes.data();
    pi.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.rasterizer_state.enable_depth_clip = true;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = true;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.depth_stencil_format = DEPTH_FORMAT;
    pi.target_info.has_depth_stencil_target = true;
    m_pipeline = SDL_CreateGPUGraphicsPipeline(&device, &pi);
    if (!m_pipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_doubleSidedPipeline = SDL_CreateGPUGraphicsPipeline(&device, &pi);
    if (!m_doubleSidedPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(double-sided)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    SDL_GPUColorTargetDescription blendTarget = ct;
    blendTarget.blend_state.enable_blend = true;
    blendTarget.blend_state.src_color_blendfactor =
        SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blendTarget.blend_state.dst_color_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blendTarget.blend_state.src_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE;
    blendTarget.blend_state.dst_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo blendPi = pi;
    blendPi.depth_stencil_state.enable_depth_write = false;
    blendPi.target_info.color_target_descriptions = &blendTarget;
    m_blendPipeline = SDL_CreateGPUGraphicsPipeline(&device, &blendPi);
    if (!m_blendPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(alpha-blended)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    blendPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_blendDoubleSidedPipeline = SDL_CreateGPUGraphicsPipeline(&device, &blendPi);
    if (!m_blendDoubleSidedPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(alpha-blended double-sided)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUGraphicsPipelineCreateInfo skyPi = pi;
    skyPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    skyPi.depth_stencil_state.enable_depth_write = false;
    skyPi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    m_skyPipeline = SDL_CreateGPUGraphicsPipeline(&device, &skyPi);
    if (!m_skyPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(skybox)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo prefilterVertexInfo{};
    prefilterVertexInfo.format = format;
    prefilterVertexInfo.entrypoint = "VSMain";
    prefilterVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShaderCreateInfo prefilterFragmentInfo{};
    prefilterFragmentInfo.format = format;
    prefilterFragmentInfo.entrypoint = "PSMain";
    prefilterFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    prefilterFragmentInfo.num_samplers = 1;
    prefilterFragmentInfo.num_uniform_buffers = 1;
    if (metal) {
        prefilterVertexInfo.code = env_prefilter_gpu_vertex_msl;
        prefilterVertexInfo.code_size = sizeof(env_prefilter_gpu_vertex_msl);
        prefilterFragmentInfo.code = env_prefilter_gpu_fragment_msl;
        prefilterFragmentInfo.code_size = sizeof(env_prefilter_gpu_fragment_msl);
    } else if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        prefilterVertexInfo.code = env_prefilter_gpu_vertex_dxil;
        prefilterVertexInfo.code_size = sizeof(env_prefilter_gpu_vertex_dxil);
        prefilterFragmentInfo.code = env_prefilter_gpu_fragment_dxil;
        prefilterFragmentInfo.code_size = sizeof(env_prefilter_gpu_fragment_dxil);
    } else {
        prefilterVertexInfo.code = env_prefilter_gpu_vertex_spirv;
        prefilterVertexInfo.code_size = sizeof(env_prefilter_gpu_vertex_spirv);
        prefilterFragmentInfo.code = env_prefilter_gpu_fragment_spirv;
        prefilterFragmentInfo.code_size = sizeof(env_prefilter_gpu_fragment_spirv);
    }
    m_environmentPrefilterVertexShader = SDL_CreateGPUShader(&device, &prefilterVertexInfo);
    m_environmentPrefilterFragmentShader = SDL_CreateGPUShader(&device, &prefilterFragmentInfo);
    if (!m_environmentPrefilterVertexShader || !m_environmentPrefilterFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(environment prefilter)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUColorTargetDescription prefilterTarget{};
    prefilterTarget.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo prefilterPi{};
    prefilterPi.vertex_shader = m_environmentPrefilterVertexShader;
    prefilterPi.fragment_shader = m_environmentPrefilterFragmentShader;
    prefilterPi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    prefilterPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    prefilterPi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    prefilterPi.target_info.num_color_targets = 1;
    prefilterPi.target_info.color_target_descriptions = &prefilterTarget;
    m_environmentPrefilterPipeline = SDL_CreateGPUGraphicsPipeline(&device, &prefilterPi);
    if (!m_environmentPrefilterPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(environment prefilter)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo surfaceMarkVertexInfo{};
    surfaceMarkVertexInfo.format = format;
    surfaceMarkVertexInfo.entrypoint = "VSMain";
    surfaceMarkVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    surfaceMarkVertexInfo.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo surfaceMarkFragmentInfo{};
    surfaceMarkFragmentInfo.format = format;
    surfaceMarkFragmentInfo.entrypoint = "PSMain";
    surfaceMarkFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    surfaceMarkFragmentInfo.num_samplers = 2;
    surfaceMarkFragmentInfo.num_uniform_buffers = 1;
    if (metal) {
        surfaceMarkVertexInfo.code = surface_mark_gpu_vertex_msl;
        surfaceMarkVertexInfo.code_size = sizeof(surface_mark_gpu_vertex_msl);
        surfaceMarkFragmentInfo.code = surface_mark_gpu_fragment_msl;
        surfaceMarkFragmentInfo.code_size = sizeof(surface_mark_gpu_fragment_msl);
    } else if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        surfaceMarkVertexInfo.code = surface_mark_gpu_vertex_dxil;
        surfaceMarkVertexInfo.code_size = sizeof(surface_mark_gpu_vertex_dxil);
        surfaceMarkFragmentInfo.code = surface_mark_gpu_fragment_dxil;
        surfaceMarkFragmentInfo.code_size = sizeof(surface_mark_gpu_fragment_dxil);
    } else {
        surfaceMarkVertexInfo.code = surface_mark_gpu_vertex_spirv;
        surfaceMarkVertexInfo.code_size = sizeof(surface_mark_gpu_vertex_spirv);
        surfaceMarkFragmentInfo.code = surface_mark_gpu_fragment_spirv;
        surfaceMarkFragmentInfo.code_size = sizeof(surface_mark_gpu_fragment_spirv);
    }
    m_surfaceMarkVertexShader = SDL_CreateGPUShader(&device, &surfaceMarkVertexInfo);
    m_surfaceMarkFragmentShader = SDL_CreateGPUShader(&device, &surfaceMarkFragmentInfo);
    SDL_GPUShaderCreateInfo surfaceMarkDepthFragmentInfo = surfaceMarkFragmentInfo;
    surfaceMarkDepthFragmentInfo.num_samplers = 0;
    surfaceMarkDepthFragmentInfo.num_uniform_buffers = 0;
    if (metal) {
        surfaceMarkDepthFragmentInfo.code = surface_mark_depth_gpu_fragment_msl;
        surfaceMarkDepthFragmentInfo.code_size = sizeof(surface_mark_depth_gpu_fragment_msl);
    } else if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        surfaceMarkDepthFragmentInfo.code = surface_mark_depth_gpu_fragment_dxil;
        surfaceMarkDepthFragmentInfo.code_size = sizeof(surface_mark_depth_gpu_fragment_dxil);
    } else {
        surfaceMarkDepthFragmentInfo.code = surface_mark_depth_gpu_fragment_spirv;
        surfaceMarkDepthFragmentInfo.code_size = sizeof(surface_mark_depth_gpu_fragment_spirv);
    }
    m_surfaceMarkDepthFragmentShader = SDL_CreateGPUShader(&device, &surfaceMarkDepthFragmentInfo);
    if (!m_surfaceMarkVertexShader || !m_surfaceMarkFragmentShader || !m_surfaceMarkDepthFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(surface mark)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUVertexBufferDescription surfaceMarkVb{};
    surfaceMarkVb.slot = 0;
    surfaceMarkVb.pitch = sizeof(SdlBasicVertex);
    surfaceMarkVb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    std::array<SDL_GPUVertexAttribute, 4> surfaceMarkAttributes{};
    surfaceMarkAttributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                static_cast<Uint32>(offsetof(SdlBasicVertex, position))};
    surfaceMarkAttributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                static_cast<Uint32>(offsetof(SdlBasicVertex, uv))};
    surfaceMarkAttributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                                static_cast<Uint32>(offsetof(SdlBasicVertex, color))};
    surfaceMarkAttributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                static_cast<Uint32>(offsetof(SdlBasicVertex, normal))};
    SDL_GPUGraphicsPipelineCreateInfo surfaceMarkPi{};
    surfaceMarkPi.vertex_shader = m_surfaceMarkVertexShader;
    surfaceMarkPi.fragment_shader = m_surfaceMarkFragmentShader;
    surfaceMarkPi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    surfaceMarkPi.vertex_input_state.vertex_buffer_descriptions = &surfaceMarkVb;
    surfaceMarkPi.vertex_input_state.num_vertex_buffers = 1;
    surfaceMarkPi.vertex_input_state.vertex_attributes = surfaceMarkAttributes.data();
    surfaceMarkPi.vertex_input_state.num_vertex_attributes =
        static_cast<Uint32>(surfaceMarkAttributes.size());
    surfaceMarkPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    surfaceMarkPi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    surfaceMarkPi.depth_stencil_state.enable_depth_test = true;
    surfaceMarkPi.depth_stencil_state.enable_depth_write = true;
    surfaceMarkPi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    surfaceMarkPi.target_info.depth_stencil_format = DEPTH_FORMAT;
    surfaceMarkPi.target_info.has_depth_stencil_target = true;
    surfaceMarkPi.fragment_shader = m_surfaceMarkDepthFragmentShader;
    m_surfaceMarkDepthPipeline = SDL_CreateGPUGraphicsPipeline(&device, &surfaceMarkPi);
    if (!m_surfaceMarkDepthPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(surface mark depth)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUColorTargetDescription surfaceMarkTarget{};
    surfaceMarkTarget.format = COLOR_FORMAT;
    surfaceMarkPi.target_info.num_color_targets = 1;
    surfaceMarkPi.target_info.color_target_descriptions = &surfaceMarkTarget;
    surfaceMarkPi.fragment_shader = m_surfaceMarkFragmentShader;
    surfaceMarkPi.depth_stencil_state.enable_depth_write = false;
    surfaceMarkPi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    surfaceMarkPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_surfaceMarkPipeline = SDL_CreateGPUGraphicsPipeline(&device, &surfaceMarkPi);
    if (!m_surfaceMarkPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(surface mark)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUGraphicsPipelineCreateInfo shadowPi = pi;
    SDL_GPUShaderCreateInfo shadowShaderInfo{};
    shadowShaderInfo.format = fi.format;
    shadowShaderInfo.entrypoint = "PSMain";
    shadowShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    shadowShaderInfo.num_samplers = 1;
    shadowShaderInfo.num_uniform_buffers = 1;
    if (metal) {
        shadowShaderInfo.code = shadow_gpu_fragment_msl;
        shadowShaderInfo.code_size = sizeof(shadow_gpu_fragment_msl);
    } else if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
        shadowShaderInfo.code = shadow_gpu_fragment_dxil;
        shadowShaderInfo.code_size = sizeof(shadow_gpu_fragment_dxil);
    } else {
        shadowShaderInfo.code = shadow_gpu_fragment_spirv;
        shadowShaderInfo.code_size = sizeof(shadow_gpu_fragment_spirv);
    }
    m_shadowFragmentShader = SDL_CreateGPUShader(&device, &shadowShaderInfo);
    if (!m_shadowFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(shadow fragment)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    shadowPi.fragment_shader = m_shadowFragmentShader;
    shadowPi.target_info.num_color_targets = 0;
    shadowPi.target_info.color_target_descriptions = nullptr;
    shadowPi.depth_stencil_state.enable_depth_write = true;
    shadowPi.target_info.depth_stencil_format = DEPTH_FORMAT;
    shadowPi.target_info.has_depth_stencil_target = true;
    // Chunks include faces with the legacy terrain winding.
    shadowPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_shadowPipeline = SDL_CreateGPUGraphicsPipeline(&device, &shadowPi);
    if (!m_shadowPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(shadow)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo selectionShaderInfo{};
    selectionShaderInfo.format = fi.format;
    selectionShaderInfo.entrypoint = "PSMain";
    selectionShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    selectionShaderInfo.num_samplers = 1;
    selectionShaderInfo.num_uniform_buffers = 1;
    if (metal) {
        selectionShaderInfo.code = selection_mask_gpu_fragment_msl;
        selectionShaderInfo.code_size = sizeof(selection_mask_gpu_fragment_msl);
    } else if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
        selectionShaderInfo.code = selection_mask_gpu_fragment_dxil;
        selectionShaderInfo.code_size = sizeof(selection_mask_gpu_fragment_dxil);
    } else {
        selectionShaderInfo.code = selection_mask_gpu_fragment_spirv;
        selectionShaderInfo.code_size = sizeof(selection_mask_gpu_fragment_spirv);
    }
    m_selectionMaskFragmentShader = SDL_CreateGPUShader(&device, &selectionShaderInfo);
    if (!m_selectionMaskFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(selection mask)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUGraphicsPipelineCreateInfo maskPi = pi;
    SDL_GPUColorTargetDescription maskTarget{};
    maskTarget.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    maskPi.fragment_shader = m_selectionMaskFragmentShader;
    maskPi.target_info.color_target_descriptions = &maskTarget;
    maskPi.depth_stencil_state.enable_depth_write = false;
    maskPi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    m_selectionMaskPipeline = SDL_CreateGPUGraphicsPipeline(&device, &maskPi);
    if (!m_selectionMaskPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(selection mask)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    maskPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_selectionMaskDoubleSidedPipeline = SDL_CreateGPUGraphicsPipeline(&device, &maskPi);
    if (!m_selectionMaskDoubleSidedPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(selection mask double-sided)");
        releaseResources();
        m_device = nullptr;
        return false;
    }

    SDL_GPUShaderCreateInfo outlineVertexInfo{};
    outlineVertexInfo.format = fi.format;
    outlineVertexInfo.entrypoint = "VSMain";
    outlineVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    if (metal) {
        outlineVertexInfo.code = post_effect_gpu_vertex_msl;
        outlineVertexInfo.code_size = sizeof(post_effect_gpu_vertex_msl);
    } else if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
        outlineVertexInfo.code = post_effect_gpu_vertex_dxil;
        outlineVertexInfo.code_size = sizeof(post_effect_gpu_vertex_dxil);
    } else {
        outlineVertexInfo.code = post_effect_gpu_vertex_spirv;
        outlineVertexInfo.code_size = sizeof(post_effect_gpu_vertex_spirv);
    }
    m_selectionOutlineVertexShader = SDL_CreateGPUShader(&device, &outlineVertexInfo);
    if (!m_selectionOutlineVertexShader) {
        error = sdlError("SDL_CreateGPUShader(selection outline vertex)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo outlineFragmentInfo{};
    outlineFragmentInfo.format = fi.format;
    outlineFragmentInfo.entrypoint = "PSMain";
    outlineFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    outlineFragmentInfo.num_samplers = 1;
    outlineFragmentInfo.num_uniform_buffers = 1;
    if (metal) {
        outlineFragmentInfo.code = selection_outline_gpu_fragment_msl;
        outlineFragmentInfo.code_size = sizeof(selection_outline_gpu_fragment_msl);
    } else if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
        outlineFragmentInfo.code = selection_outline_gpu_fragment_dxil;
        outlineFragmentInfo.code_size = sizeof(selection_outline_gpu_fragment_dxil);
    } else {
        outlineFragmentInfo.code = selection_outline_gpu_fragment_spirv;
        outlineFragmentInfo.code_size = sizeof(selection_outline_gpu_fragment_spirv);
    }
    m_selectionOutlineFragmentShader = SDL_CreateGPUShader(&device, &outlineFragmentInfo);
    if (!m_selectionOutlineFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(selection outline fragment)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUGraphicsPipelineCreateInfo outlinePi{};
    outlinePi.vertex_shader = m_selectionOutlineVertexShader;
    outlinePi.fragment_shader = m_selectionOutlineFragmentShader;
    outlinePi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    outlinePi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    outlinePi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    outlinePi.target_info.num_color_targets = 1;
    SDL_GPUColorTargetDescription outlineTarget = ct;
    outlineTarget.blend_state.enable_blend = true;
    outlineTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    outlineTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    outlineTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    outlineTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    outlineTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    outlineTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    outlinePi.target_info.color_target_descriptions = &outlineTarget;
    m_selectionOutlinePipeline = SDL_CreateGPUGraphicsPipeline(&device, &outlinePi);
    if (!m_selectionOutlinePipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(selection outline)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo guideVertexInfo{};
    guideVertexInfo.format = fi.format;
    guideVertexInfo.entrypoint = "VSMain";
    guideVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    guideVertexInfo.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo guideFragmentInfo{};
    guideFragmentInfo.format = fi.format;
    guideFragmentInfo.entrypoint = "PSMain";
    guideFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    if (metal) {
        guideVertexInfo.code = terrain_guide_gpu_vertex_msl;
        guideVertexInfo.code_size = sizeof(terrain_guide_gpu_vertex_msl);
        guideFragmentInfo.code = terrain_guide_gpu_fragment_msl;
        guideFragmentInfo.code_size = sizeof(terrain_guide_gpu_fragment_msl);
    } else if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
        guideVertexInfo.code = terrain_guide_gpu_vertex_dxil;
        guideVertexInfo.code_size = sizeof(terrain_guide_gpu_vertex_dxil);
        guideFragmentInfo.code = terrain_guide_gpu_fragment_dxil;
        guideFragmentInfo.code_size = sizeof(terrain_guide_gpu_fragment_dxil);
    } else {
        guideVertexInfo.code = terrain_guide_gpu_vertex_spirv;
        guideVertexInfo.code_size = sizeof(terrain_guide_gpu_vertex_spirv);
        guideFragmentInfo.code = terrain_guide_gpu_fragment_spirv;
        guideFragmentInfo.code_size = sizeof(terrain_guide_gpu_fragment_spirv);
    }
    m_terrainGuideVertexShader = SDL_CreateGPUShader(&device, &guideVertexInfo);
    m_terrainGuideFragmentShader = SDL_CreateGPUShader(&device, &guideFragmentInfo);
    if (!m_terrainGuideVertexShader || !m_terrainGuideFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(terrain guide)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUGraphicsPipelineCreateInfo guidePi{};
    guidePi.vertex_shader = m_terrainGuideVertexShader;
    guidePi.fragment_shader = m_terrainGuideFragmentShader;
    guidePi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    guidePi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    guidePi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    guidePi.depth_stencil_state.enable_depth_test = true;
    guidePi.depth_stencil_state.enable_depth_write = false;
    guidePi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    guidePi.target_info.num_color_targets = 1;
    guidePi.target_info.color_target_descriptions = &ct;
    guidePi.target_info.depth_stencil_format = DEPTH_FORMAT;
    guidePi.target_info.has_depth_stencil_target = true;
    m_terrainGuidePipeline = SDL_CreateGPUGraphicsPipeline(&device, &guidePi);
    if (!m_terrainGuidePipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(terrain guide)");
        releaseResources();
        m_device = nullptr;
        return false;
    }

    SDL_GPUShaderCreateInfo celestialVertexInfo{};
    celestialVertexInfo.format = fi.format;
    celestialVertexInfo.entrypoint = "VSMain";
    celestialVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    celestialVertexInfo.num_uniform_buffers = 1;
    celestialVertexInfo.num_samplers = 1;
    SDL_GPUShaderCreateInfo celestialFragmentInfo{};
    celestialFragmentInfo.format = fi.format;
    celestialFragmentInfo.entrypoint = "PSMain";
    celestialFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    celestialFragmentInfo.num_samplers = 1;
    celestialFragmentInfo.num_uniform_buffers = 1;
    if (metal) {
        celestialVertexInfo.code = celestial_gpu_vertex_msl;
        celestialVertexInfo.code_size = sizeof(celestial_gpu_vertex_msl);
        celestialFragmentInfo.code = celestial_gpu_fragment_msl;
        celestialFragmentInfo.code_size = sizeof(celestial_gpu_fragment_msl);
    } else if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
        celestialVertexInfo.code = celestial_gpu_vertex_dxil;
        celestialVertexInfo.code_size = sizeof(celestial_gpu_vertex_dxil);
        celestialFragmentInfo.code = celestial_gpu_fragment_dxil;
        celestialFragmentInfo.code_size = sizeof(celestial_gpu_fragment_dxil);
    } else {
        celestialVertexInfo.code = celestial_gpu_vertex_spirv;
        celestialVertexInfo.code_size = sizeof(celestial_gpu_vertex_spirv);
        celestialFragmentInfo.code = celestial_gpu_fragment_spirv;
        celestialFragmentInfo.code_size = sizeof(celestial_gpu_fragment_spirv);
    }
    m_celestialVertexShader = SDL_CreateGPUShader(&device, &celestialVertexInfo);
    m_celestialFragmentShader = SDL_CreateGPUShader(&device, &celestialFragmentInfo);
    if (!m_celestialVertexShader || !m_celestialFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(celestial)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUGraphicsPipelineCreateInfo celestialPi{};
    celestialPi.vertex_shader = m_celestialVertexShader;
    celestialPi.fragment_shader = m_celestialFragmentShader;
    celestialPi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    celestialPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    celestialPi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    celestialPi.target_info.num_color_targets = 1;
    SDL_GPUColorTargetDescription celestialTarget = ct;
    celestialTarget.blend_state.enable_blend = true;
    celestialTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    celestialTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    celestialTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    celestialTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    celestialTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    celestialTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    celestialPi.target_info.color_target_descriptions = &celestialTarget;
    m_celestialDiscPipeline = SDL_CreateGPUGraphicsPipeline(&device, &celestialPi);
    if (!m_celestialDiscPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(celestial disc)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUColorTargetDescription additiveTarget = celestialTarget;
    additiveTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    additiveTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    additiveTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    additiveTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    celestialPi.target_info.color_target_descriptions = &additiveTarget;
    m_celestialAdditivePipeline = SDL_CreateGPUGraphicsPipeline(&device, &celestialPi);
    if (!m_celestialAdditivePipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(celestial additive)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    m_sampler = SDL_CreateGPUSampler(&device, &samplerInfo);
    if (!m_sampler) {
        error = sdlError("SDL_CreateGPUSampler");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo skySamplerInfo = samplerInfo;
    skySamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    skySamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    skySamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_skySampler = SDL_CreateGPUSampler(&device, &skySamplerInfo);
    if (!m_skySampler) {
        error = sdlError("SDL_CreateGPUSampler(skybox)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_environmentPrefilterSampler = SDL_CreateGPUSampler(&device, &skySamplerInfo);
    if (!m_environmentPrefilterSampler) {
        error = sdlError("SDL_CreateGPUSampler(environment prefilter)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo surfaceMarkSamplerInfo = skySamplerInfo;
    m_surfaceMarkSampler = SDL_CreateGPUSampler(&device, &surfaceMarkSamplerInfo);
    if (!m_surfaceMarkSampler) {
        error = sdlError("SDL_CreateGPUSampler(surface mark)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo surfaceMarkDepthSamplerInfo = skySamplerInfo;
    surfaceMarkDepthSamplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    surfaceMarkDepthSamplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    surfaceMarkDepthSamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    m_surfaceMarkDepthSampler = SDL_CreateGPUSampler(&device, &surfaceMarkDepthSamplerInfo);
    if (!m_surfaceMarkDepthSampler) {
        error = sdlError("SDL_CreateGPUSampler(surface mark depth)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo shadowSamplerInfo{};
    shadowSamplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    shadowSamplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    shadowSamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    shadowSamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadowSamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadowSamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_shadowSampler = SDL_CreateGPUSampler(&device, &shadowSamplerInfo);
    if (!m_shadowSampler) {
        error = sdlError("SDL_CreateGPUSampler(shadow)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo selectionSamplerInfo{};
    selectionSamplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    selectionSamplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    selectionSamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    selectionSamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    selectionSamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    selectionSamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_selectionSampler = SDL_CreateGPUSampler(&device, &selectionSamplerInfo);
    if (!m_selectionSampler) {
        error = sdlError("SDL_CreateGPUSampler(selection)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    std::array<SdlBasicVertex, 24> faceCubeVertices{};
    if (!makeFaceCubeVertices(faceCubeVertices, error)) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    const auto handle = uploadMesh(faceCubeVertices, FACE_CUBE_INDICES, error);
    if (!handle) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    const auto defaultGeometry = m_geometries.find(handle);
    if (defaultGeometry == m_geometries.end()) {
        error = "SdlBasicRenderer: default geometry handle was not retained";
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_defaultGeometry = defaultGeometry->second;
    m_geometries.erase(defaultGeometry);
    const std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
    const auto whiteHandle = uploadTexture(1, 1, white, error);
    if (!whiteHandle) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_whiteTexture = m_textures.at(whiteHandle);

    SDL_GPUShaderCreateInfo cloudVertexInfo{};
    cloudVertexInfo.format = format;
    cloudVertexInfo.entrypoint = "VSMain";
    cloudVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    cloudVertexInfo.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo cloudFragmentInfo{};
    cloudFragmentInfo.format = format;
    cloudFragmentInfo.entrypoint = "PSMain";
    cloudFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    cloudFragmentInfo.num_samplers = 1;
    cloudFragmentInfo.num_uniform_buffers = 1;
    if (metal) {
        cloudVertexInfo.code = cloud_gpu_vertex_msl;
        cloudVertexInfo.code_size = sizeof(cloud_gpu_vertex_msl);
        cloudFragmentInfo.code = cloud_gpu_fragment_msl;
        cloudFragmentInfo.code_size = sizeof(cloud_gpu_fragment_msl);
    } else if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        cloudVertexInfo.code = cloud_gpu_vertex_dxil;
        cloudVertexInfo.code_size = sizeof(cloud_gpu_vertex_dxil);
        cloudFragmentInfo.code = cloud_gpu_fragment_dxil;
        cloudFragmentInfo.code_size = sizeof(cloud_gpu_fragment_dxil);
    } else {
        cloudVertexInfo.code = cloud_gpu_vertex_spirv;
        cloudVertexInfo.code_size = sizeof(cloud_gpu_vertex_spirv);
        cloudFragmentInfo.code = cloud_gpu_fragment_spirv;
        cloudFragmentInfo.code_size = sizeof(cloud_gpu_fragment_spirv);
    }
    m_cloudVertexShader = SDL_CreateGPUShader(&device, &cloudVertexInfo);
    m_cloudFragmentShader = SDL_CreateGPUShader(&device, &cloudFragmentInfo);
    if (!m_cloudVertexShader || !m_cloudFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(cloud)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUVertexBufferDescription cloudVb{};
    cloudVb.slot = 0;
    cloudVb.pitch = sizeof(SdlBasicVertex);
    cloudVb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    std::array<SDL_GPUVertexAttribute, 2> cloudAttributes{};
    cloudAttributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                          static_cast<Uint32>(offsetof(SdlBasicVertex, position))};
    cloudAttributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                          static_cast<Uint32>(offsetof(SdlBasicVertex, uv))};
    SDL_GPUGraphicsPipelineCreateInfo cloudPi{};
    cloudPi.vertex_shader = m_cloudVertexShader;
    cloudPi.fragment_shader = m_cloudFragmentShader;
    cloudPi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    cloudPi.vertex_input_state.vertex_buffer_descriptions = &cloudVb;
    cloudPi.vertex_input_state.num_vertex_buffers = 1;
    cloudPi.vertex_input_state.vertex_attributes = cloudAttributes.data();
    cloudPi.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(cloudAttributes.size());
    cloudPi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    cloudPi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    cloudPi.rasterizer_state.enable_depth_clip = true;
    cloudPi.depth_stencil_state.enable_depth_test = true;
    cloudPi.depth_stencil_state.enable_depth_write = false;
    cloudPi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    SDL_GPUColorTargetDescription cloudTarget = ct;
    cloudTarget.blend_state.enable_blend = true;
    cloudTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    cloudTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cloudTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    cloudTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    cloudTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cloudTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    cloudPi.target_info.num_color_targets = 1;
    cloudPi.target_info.color_target_descriptions = &cloudTarget;
    cloudPi.target_info.depth_stencil_format = DEPTH_FORMAT;
    cloudPi.target_info.has_depth_stencil_target = true;
    m_cloudPipeline = SDL_CreateGPUGraphicsPipeline(&device, &cloudPi);
    if (!m_cloudPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(cloud)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo particleVsInfo{};
    particleVsInfo.format = fi.format;
    particleVsInfo.entrypoint = "VSMain";
    particleVsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    particleVsInfo.num_uniform_buffers = 1;
    particleVsInfo.code = metal ? particle_gpu_vertex_msl :
        (fi.format == SDL_GPU_SHADERFORMAT_DXIL ? particle_gpu_vertex_dxil : particle_gpu_vertex_spirv);
    particleVsInfo.code_size = metal ? sizeof(particle_gpu_vertex_msl) :
        (fi.format == SDL_GPU_SHADERFORMAT_DXIL ? sizeof(particle_gpu_vertex_dxil) : sizeof(particle_gpu_vertex_spirv));
    m_particleVertexShader = SDL_CreateGPUShader(&device, &particleVsInfo);
    SDL_GPUShaderCreateInfo particleFsInfo{};
    particleFsInfo.format = fi.format;
    particleFsInfo.entrypoint = "PSMain";
    particleFsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    particleFsInfo.code = metal ? particle_gpu_fragment_msl :
        (fi.format == SDL_GPU_SHADERFORMAT_DXIL ? particle_gpu_fragment_dxil : particle_gpu_fragment_spirv);
    particleFsInfo.code_size = metal ? sizeof(particle_gpu_fragment_msl) :
        (fi.format == SDL_GPU_SHADERFORMAT_DXIL ? sizeof(particle_gpu_fragment_dxil) : sizeof(particle_gpu_fragment_spirv));
    m_particleFragmentShader = SDL_CreateGPUShader(&device, &particleFsInfo);
    if (!m_particleVertexShader || !m_particleFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(particle)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUVertexBufferDescription particleVb{};
    particleVb.slot = 0;
    particleVb.pitch = sizeof(SdlParticleVertex);
    std::array<SDL_GPUVertexAttribute, 2> particleAttributes = {{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, sizeof(float) * 3}}};
    SDL_GPUColorTargetDescription particleTarget = ct;
    particleTarget.blend_state.enable_blend = true;
    particleTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    particleTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    particleTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    particleTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    particleTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    particleTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo particlePi{};
    particlePi.vertex_shader = m_particleVertexShader;
    particlePi.fragment_shader = m_particleFragmentShader;
    particlePi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    particlePi.vertex_input_state.vertex_buffer_descriptions = &particleVb;
    particlePi.vertex_input_state.num_vertex_buffers = 1;
    particlePi.vertex_input_state.vertex_attributes = particleAttributes.data();
    particlePi.vertex_input_state.num_vertex_attributes = 2;
    particlePi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    particlePi.target_info.num_color_targets = 1;
    particlePi.target_info.color_target_descriptions = &particleTarget;
    particlePi.target_info.depth_stencil_format = DEPTH_FORMAT;
    particlePi.target_info.has_depth_stencil_target = true;
    particlePi.depth_stencil_state.enable_depth_test = true;
    particlePi.depth_stencil_state.enable_depth_write = false;
    particlePi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    m_particlePipeline = SDL_CreateGPUGraphicsPipeline(&device, &particlePi);
    if (!m_particlePipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(particle)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    const std::array<SdlBasicVertex, 6> cloudVertices = {{
        {{{-1.0f, 0.0f, -1.0f}}, {{0.0f, 0.0f}}}, {{{1.0f, 0.0f, -1.0f}}, {{8.0f, 0.0f}}},
        {{{1.0f, 0.0f, 1.0f}}, {{8.0f, 8.0f}}}, {{{1.0f, 0.0f, 1.0f}}, {{8.0f, 8.0f}}},
        {{{-1.0f, 0.0f, 1.0f}}, {{0.0f, 8.0f}}}, {{{-1.0f, 0.0f, -1.0f}}, {{0.0f, 0.0f}}}}};
    const std::array<std::uint32_t, 6> cloudIndices = {0, 1, 2, 3, 4, 5};
    const auto cloudGeometryHandle = uploadMesh(cloudVertices, cloudIndices, error);
    if (!cloudGeometryHandle) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    auto cloudGeometry = m_geometries.find(cloudGeometryHandle);
    if (cloudGeometry == m_geometries.end()) {
        error = "SdlBasicRenderer: cloud geometry handle was not retained";
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_cloudGeometry = cloudGeometry->second;
    m_geometries.erase(cloudGeometry);
    constexpr unsigned cloudSize = 512;
    PerlinNoise cloudNoise(1337u);
    std::vector<std::uint8_t> cloudPixels(static_cast<std::size_t>(cloudSize) * cloudSize * 4);
    for (unsigned y = 0; y < cloudSize; ++y) {
        for (unsigned x = 0; x < cloudSize; ++x) {
            const float u = static_cast<float>(x) / cloudSize;
            const float v = static_cast<float>(y) / cloudSize;
            const float fu = u * 3.0f;
            const float fv = v * 3.0f;
            const float n00 = cloudNoise.fbm2(fu, fv);
            const float n10 = cloudNoise.fbm2(fu - 3.0f, fv);
            const float n01 = cloudNoise.fbm2(fu, fv - 3.0f);
            const float n11 = cloudNoise.fbm2(fu - 3.0f, fv - 3.0f);
            const float n = n00 * (1 - u) * (1 - v) + n10 * u * (1 - v) + n01 * (1 - u) * v + n11 * u * v;
            const auto value = static_cast<std::uint8_t>(std::clamp(n * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            const std::size_t index = (static_cast<std::size_t>(y) * cloudSize + x) * 4;
            cloudPixels[index] = cloudPixels[index + 1] = cloudPixels[index + 2] = value;
            cloudPixels[index + 3] = 255;
        }
    }
    m_cloudNoiseTexture = uploadSdlRgbaTexture(device, cloudSize, cloudSize, cloudPixels, error);
    if (!m_cloudNoiseTexture) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUSamplerCreateInfo cloudSamplerInfo = samplerInfo;
    cloudSamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    cloudSamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    m_cloudSampler = SDL_CreateGPUSampler(&device, &cloudSamplerInfo);
    if (!m_cloudSampler) {
        error = sdlError("SDL_CreateGPUSampler(cloud)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_environmentFallbackCubemap = createFallbackCubemap(device, error);
    if (!m_environmentFallbackCubemap) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_skyboxSourceCubemap = createSkyboxTarget(device,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        COLOR_FORMAT, error);
    if (!m_skyboxSourceCubemap) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_skyboxSourceDepth = createSkyboxTarget(device, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                             DEPTH_FORMAT, error);
    if (!m_skyboxSourceDepth) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUTextureCreateInfo surfaceMarkDepthInfo{};
    surfaceMarkDepthInfo.type = SDL_GPU_TEXTURETYPE_2D;
    surfaceMarkDepthInfo.format = DEPTH_FORMAT;
    surfaceMarkDepthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                 SDL_GPU_TEXTUREUSAGE_SAMPLER;
    surfaceMarkDepthInfo.width = SURFACE_MARK_MAP_SIZE;
    surfaceMarkDepthInfo.height = SURFACE_MARK_MAP_SIZE;
    surfaceMarkDepthInfo.layer_count_or_depth = 1;
    surfaceMarkDepthInfo.num_levels = 1;
    m_surfaceMarkDepthTarget = SDL_CreateGPUTexture(&device, &surfaceMarkDepthInfo);
    if (!m_surfaceMarkDepthTarget) {
        error = sdlError("SDL_CreateGPUTexture(surface mark depth)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    return true;
}

std::uint32_t SdlBasicRenderer::uploadMesh(std::span<const SdlBasicVertex> vertices,
                                           std::span<const std::uint32_t> indices, std::string& error) {
    error.clear();
    if (!m_device) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return 0;
    }
    if (m_nextGeometryHandle == 0) {
        error = "SdlBasicRenderer: geometry handle space exhausted";
        return 0;
    }
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) {
        error = "SdlBasicRenderer: mesh must contain vertices and triangle indices";
        return 0;
    }
    for (const auto& vertex : vertices) {
        for (float coordinate : vertex.position) {
            if (!std::isfinite(coordinate)) {
                error = "SdlBasicRenderer: mesh contains a non-finite vertex coordinate";
                return 0;
            }
        }
        for (float coordinate : vertex.uv) {
            if (!std::isfinite(coordinate)) {
                error = "SdlBasicRenderer: mesh contains a non-finite texture coordinate";
                return 0;
            }
        }
    }
    for (auto i : indices) {
        if (i >= vertices.size()) {
            error = "SdlBasicRenderer: mesh index is out of range";
            return 0;
        }
    }
    constexpr size_t maxBufferSize = std::numeric_limits<Uint32>::max();
    if (vertices.size() > maxBufferSize / sizeof(vertices[0]) ||
        indices.size() > maxBufferSize / sizeof(indices[0])) {
        error = "SdlBasicRenderer: mesh is too large";
        return 0;
    }
    const size_t vertexBytes = vertices.size() * sizeof(vertices[0]);
    if (vertexBytes > maxBufferSize - 255u) {
        error = "SdlBasicRenderer: mesh is too large";
        return 0;
    }
    const size_t indexOffset = (vertexBytes + 255u) & ~size_t(255u);
    const size_t indexBytes = indices.size() * sizeof(indices[0]);
    if (indexOffset > maxBufferSize - indexBytes || indices.size() > maxBufferSize) {
        error = "SdlBasicRenderer: mesh is too large";
        return 0;
    }
    const size_t total = indexOffset + indexBytes;
    SDL_GPUBufferCreateInfo vertexInfo{};
    vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertexInfo.size = static_cast<Uint32>(vertexBytes);
    SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(m_device, &vertexInfo);
    if (!vertexBuffer) {
        error = sdlError("SDL_CreateGPUBuffer(vertex)");
        return 0;
    }
    SDL_GPUBufferCreateInfo indexInfo{};
    indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    indexInfo.size = static_cast<Uint32>(indexBytes);
    SDL_GPUBuffer* indexBuffer = SDL_CreateGPUBuffer(m_device, &indexInfo);
    if (!indexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        error = sdlError("SDL_CreateGPUBuffer(index)");
        return 0;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(total);
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
    if (!transferBuffer) {
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        SDL_ReleaseGPUBuffer(m_device, indexBuffer);
        error = sdlError("SDL_CreateGPUTransferBuffer");
        return 0;
    }
    void* data = SDL_MapGPUTransferBuffer(m_device, transferBuffer, false);
    if (!data) {
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        SDL_ReleaseGPUBuffer(m_device, indexBuffer);
        error = sdlError("SDL_MapGPUTransferBuffer");
        return 0;
    }
    std::memcpy(data, vertices.data(), vertexBytes);
    std::memcpy(static_cast<std::byte*>(data) + indexOffset, indices.data(), indexBytes);
    SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(m_device);
    if (!command) {
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        SDL_ReleaseGPUBuffer(m_device, indexBuffer);
        error = sdlError("SDL_AcquireGPUCommandBuffer");
        return 0;
    }
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(command);
    if (!pass) {
        SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        SDL_ReleaseGPUBuffer(m_device, indexBuffer);
        error = sdlError("SDL_BeginGPUCopyPass");
        return 0;
    }
    SDL_GPUTransferBufferLocation source{transferBuffer, 0};
    SDL_GPUBufferRegion destination{vertexBuffer, 0, static_cast<Uint32>(vertexBytes)};
    SDL_UploadToGPUBuffer(pass, &source, &destination, false);
    source.offset = static_cast<Uint32>(indexOffset);
    destination = SDL_GPUBufferRegion{indexBuffer, 0, static_cast<Uint32>(indexBytes)};
    SDL_UploadToGPUBuffer(pass, &source, &destination, false);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    bool uploaded = fence != nullptr && SDL_WaitForGPUFences(m_device, true, &fence, 1);
    if (fence) {
        SDL_ReleaseGPUFence(m_device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(m_device, transferBuffer);
    if (!uploaded) {
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        SDL_ReleaseGPUBuffer(m_device, indexBuffer);
        error = sdlError("SDL_SubmitGPUCommandBufferAndAcquireFence");
        return 0;
    }
    const std::uint32_t handle = m_nextGeometryHandle++;
    const auto [it, inserted] = m_geometries.emplace(
        handle, Geometry{vertexBuffer, indexBuffer, static_cast<Uint32>(indices.size())});
    if (!inserted) {
        SDL_ReleaseGPUBuffer(m_device, vertexBuffer);
        SDL_ReleaseGPUBuffer(m_device, indexBuffer);
        error = "SdlBasicRenderer: geometry handle collision";
        return 0;
    }
    return it->first;
}

bool SdlBasicRenderer::releaseMesh(std::uint32_t handle, std::string& error) {
    error.clear();
    if (!m_device) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return false;
    }
    if (handle == 0) {
        error = "SdlBasicRenderer: default geometry cannot be released";
        return false;
    }
    const auto it = m_geometries.find(handle);
    if (it == m_geometries.end()) {
        error = "SdlBasicRenderer: invalid geometry handle";
        return false;
    }
    if (it->second.vertexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, it->second.vertexBuffer);
    }
    if (it->second.indexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, it->second.indexBuffer);
    }
    m_geometries.erase(it);
    return true;
}

std::uint32_t SdlBasicRenderer::uploadTexture(unsigned width, unsigned height,
                                              std::span<const std::uint8_t> rgba,
                                              std::string& error) {
    error.clear();
    if (!m_device) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return 0;
    }
    if (m_nextTextureHandle == 0) {
        error = "SdlBasicRenderer: texture handle space exhausted";
        return 0;
    }
    SDL_GPUTexture* texture = uploadSdlRgbaTexture(*m_device, width, height, rgba, error);
    if (!texture) {
        error = "SdlBasicRenderer: " + error;
        return 0;
    }
    const auto handle = m_nextTextureHandle++;
    m_textures.emplace(handle, texture);
    return handle;
}

bool SdlBasicRenderer::releaseTexture(std::uint32_t handle, std::string& error) {
    error.clear();
    if (!m_device) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return false;
    }
    const auto it = m_textures.find(handle);
    if (it == m_textures.end() || it->second == m_whiteTexture) {
        error = "SdlBasicRenderer: invalid texture handle";
        return false;
    }
    SDL_ReleaseGPUTexture(m_device, it->second);
    m_textures.erase(it);
    return true;
}

bool SdlBasicRenderer::ensureTargets(unsigned width, unsigned height, std::string& error) {
    if (!width || !height) {
        error = "SdlBasicRenderer: render target dimensions must be non-zero";
        return false;
    }
    if (m_colorTarget && m_depthTarget && m_width == width && m_height == height) {
        return true;
    }
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = COLOR_FORMAT;
    ci.width = width;
    ci.height = height;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* color = SDL_CreateGPUTexture(m_device, &ci);
    if (!color) {
        error = sdlError("SDL_CreateGPUTexture(color)");
        return false;
    }
    ci.format = DEPTH_FORMAT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* depth = SDL_CreateGPUTexture(m_device, &ci);
    if (!depth) {
        SDL_ReleaseGPUTexture(m_device, color);
        error = sdlError("SDL_CreateGPUTexture(depth)");
        return false;
    }
    auto oldColor = m_colorTarget;
    auto oldDepth = m_depthTarget;
    m_colorTarget = color;
    m_depthTarget = depth;
    m_width = width;
    m_height = height;
    if (oldColor) {
        SDL_ReleaseGPUTexture(m_device, oldColor);
    }
    if (oldDepth) {
        SDL_ReleaseGPUTexture(m_device, oldDepth);
    }
    return true;
}

bool SdlBasicRenderer::ensureShadowTarget(std::string& error) {
    if (m_shadowTarget) return true;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = DEPTH_FORMAT;
    info.width = SHADOW_MAP_SIZE;
    info.height = SHADOW_MAP_SIZE;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    m_shadowTarget = SDL_CreateGPUTexture(m_device, &info);
    if (!m_shadowTarget) {
        error = sdlError("SDL_CreateGPUTexture(shadow)");
        return false;
    }
    return true;
}

bool SdlBasicRenderer::ensureSelectionTarget(unsigned width, unsigned height, std::string& error) {
    if (m_selectionMaskTarget && m_selectionWidth == width && m_selectionHeight == height) {
        return true;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* replacement = SDL_CreateGPUTexture(m_device, &info);
    if (!replacement) {
        error = sdlError("SDL_CreateGPUTexture(selection mask)");
        return false;
    }
    if (m_selectionMaskTarget) {
        SDL_ReleaseGPUTexture(m_device, m_selectionMaskTarget);
    }
    m_selectionMaskTarget = replacement;
    m_selectionWidth = width;
    m_selectionHeight = height;
    return true;
}

bool SdlBasicRenderer::drawMesh(SDL_GPURenderPass& pass, const SdlBasicCube& cube,
                                std::string& error) const {
    const Geometry* geometry = &m_defaultGeometry;
    if (cube.geometry) {
        const auto it = m_geometries.find(cube.geometry);
        if (it == m_geometries.end()) {
            error = "SdlBasicRenderer: invalid geometry handle " + std::to_string(cube.geometry);
            return false;
        }
        geometry = &it->second;
    }
    if (cube.firstIndex >= geometry->indexCount || cube.firstIndex % 3 != 0) {
        error = "SdlBasicRenderer: draw does not start at a triangle in the geometry";
        return false;
    }
    const auto remaining = geometry->indexCount - cube.firstIndex;
    const auto count = cube.indexCount ? cube.indexCount : remaining;
    if (count > remaining || count % 3 != 0) {
        error = "SdlBasicRenderer: draw index range is outside geometry";
        return false;
    }
    SDL_GPUBufferBinding vertex{geometry->vertexBuffer, 0};
    SDL_BindGPUVertexBuffers(&pass, 0, &vertex, 1);
    SDL_GPUBufferBinding index{geometry->indexBuffer, 0};
    SDL_BindGPUIndexBuffer(&pass, &index, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(&pass, count, 1, cube.firstIndex, 0, 0);
    return true;
}

SDL_GPUTexture* SdlBasicRenderer::resolveTexture(std::uint32_t handle, std::string& error) const {
    if (handle == 0) {
        if (!m_whiteTexture) {
            error = "SdlBasicRenderer: default white texture is unavailable";
            return nullptr;
        }
        return m_whiteTexture;
    }
    const auto it = m_textures.find(handle);
    if (it == m_textures.end() || !it->second) {
        error = "SdlBasicRenderer: invalid texture handle " + std::to_string(handle);
        return nullptr;
    }
    return it->second;
}

bool SdlBasicRenderer::draw(SDL_GPUCommandBuffer& commandBuffer, unsigned width, unsigned height,
                            std::span<const SdlBasicCube> cubes, std::string& error, bool shadows,
                            const SdlMaterialLighting& lighting,
                            std::span<const SdlBasicCube> skyFaces,
                            const SdlSkyboxSource* skyboxSource,
                            std::span<const SdlCelestialDraw> celestial,
                            std::span<const SdlCloudDraw> clouds,
                            std::span<const SdlParticleVertex> particles,
                            const std::array<float, 16>* particleView,
                            const std::array<float, 16>* particleProjection,
                            float effectTime,
                            std::span<const SdlSurfaceMarkDraw> surfaceMarks) {
    error.clear();
    if (!m_device || !m_pipeline || !m_defaultGeometry.vertexBuffer) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return false;
    }
    if (!ensureTargets(width, height, error)) {
        return false;
    }
    if (shadows && !ensureShadowTarget(error)) {
        return false;
    }
    enum class Pass { Scene, Sky, Shadow };
    const auto drawPass = [&](SDL_GPURenderPass* pass, Pass kind,
                              std::span<const SdlBasicCube> draws) -> bool {
        const bool shadowPass = kind == Pass::Shadow;
        const bool skyPass = kind == Pass::Sky;
        SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
        for (const auto& cube : draws) {
            if (shadowPass && !cube.castShadow) {
                continue;
            }
            SDL_GPUGraphicsPipeline* pipeline = m_shadowPipeline;
            if (!shadowPass) {
                if (skyPass) {
                    pipeline = m_skyPipeline;
                } else if (cube.blend) {
                    pipeline = cube.doubleSided ? m_blendDoubleSidedPipeline : m_blendPipeline;
                } else {
                    pipeline = cube.doubleSided ? m_doubleSidedPipeline : m_pipeline;
                }
            }
            if (pipeline != boundPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                boundPipeline = pipeline;
            }
            const auto texture = resolveTexture(cube.texture, error);
            if (!texture) {
                return false;
            }
            if (!shadowPass) {
                const auto materialTexture = resolveTexture(cube.materialBaseColor, error);
                if (!materialTexture) {
                    return false;
                }
                const auto overlayTexture = cube.faceTexture != 0
                                                ? resolveTexture(cube.faceTexture, error)
                                                : (cube.surfaceTexture ? cube.surfaceTexture
                                                                        : m_whiteTexture);
                if (!overlayTexture) {
                    return false;
                }
                const auto decalTexture = cube.meshDecals != nullptr && cube.meshDecals->texture != 0
                                               ? resolveTexture(cube.meshDecals->texture, error)
                                               : m_whiteTexture;
                if (!decalTexture) {
                    return false;
                }
                const auto roughnessTexture = resolveTexture(cube.materialRoughnessMap, error);
                const auto metallicTexture = resolveTexture(cube.materialMetallicMap, error);
                const auto normalTexture = resolveTexture(cube.materialNormalMap, error);
                if (!roughnessTexture || !metallicTexture || !normalTexture) {
                    return false;
                }
                const bool clampOverlay = skyPass &&
                    cube.faceUvScale[0] == 1.0f && cube.faceUvScale[1] == 1.0f;
                const std::array<SDL_GPUTextureSamplerBinding, 9> samplerBindings = {
                    SDL_GPUTextureSamplerBinding{texture, m_sampler},
                    SDL_GPUTextureSamplerBinding{shadows ? m_shadowTarget : m_whiteTexture, m_shadowSampler},
                    SDL_GPUTextureSamplerBinding{overlayTexture, clampOverlay ? m_skySampler : m_sampler},
                    SDL_GPUTextureSamplerBinding{materialTexture, m_sampler},
                    SDL_GPUTextureSamplerBinding{decalTexture, m_sampler},
                    SDL_GPUTextureSamplerBinding{roughnessTexture, m_sampler},
                    SDL_GPUTextureSamplerBinding{metallicTexture, m_sampler},
                    SDL_GPUTextureSamplerBinding{normalTexture, m_sampler},
                    SDL_GPUTextureSamplerBinding{m_skyboxPrefilteredCubemap
                                                     ? m_skyboxPrefilteredCubemap
                                                     : m_environmentFallbackCubemap,
                                                 m_environmentPrefilterSampler}};
                SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings.data(),
                                            static_cast<Uint32>(samplerBindings.size()));
            } else {
                SDL_GPUTextureSamplerBinding binding{texture, m_sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            }
            std::array<float, 56> u{};
            std::copy(cube.modelViewProjection.begin(), cube.modelViewProjection.end(), u.begin());
            std::copy(cube.shadowModelViewProjection.begin(), cube.shadowModelViewProjection.end(),
                      u.begin() + 16);
            std::copy(cube.color.begin(), cube.color.end(), u.begin() + 32);
            u[36] = shadowPass ? 1.0f : 0.0f;
            u[37] = skyPass ? 1.0f : 0.0f;
            u[38] = effectTime;
            u[39] = cube.liquid ? 1.0f : 0.0f;
            std::copy(cube.modelMatrix.begin(), cube.modelMatrix.end(), u.begin() + 40);
            SDL_PushGPUVertexUniformData(&commandBuffer, 0, u.data(), sizeof(u));
            if (shadowPass) {
                const std::array<float, 4> shadowParams = {
                    0.0f, 0.0f, cube.alphaThreshold, cube.useTextureAlpha ? 1.0f : 0.0f};
                SDL_PushGPUFragmentUniformData(&commandBuffer, 0, shadowParams.data(),
                                               sizeof(shadowParams));
            } else {
                std::array<float, 24 + SDL_MESH_DECAL_LIMIT * 8> fragmentParams = {
                    (shadows && cube.receiveShadow) ? 1.0f : 0.0f,
                    cube.faceTexture != 0 ? 2.0f : (cube.surfaceTexture ? 1.0f : 0.0f),
                    cube.alphaThreshold,
                    cube.useTextureAlpha ? 1.0f : 0.0f,
                    cube.materialTileSize,
                    cube.materialBaseColor != 0 ? 1.0f : 0.0f,
                    cube.materialMetallic,
                    cube.materialRoughness,
                    cube.faceTint[0],
                    cube.faceTint[1],
                    cube.faceTint[2],
                    cube.faceTint[3],
                    cube.faceUvScale[0],
                    cube.faceUvScale[1],
                    cube.materialReflectance,
                    cube.materialNormalStrength};
                if (cube.meshDecals != nullptr) {
                    const auto& decals = *cube.meshDecals;
                    fragmentParams[16] = decals.minimum[0];
                    fragmentParams[17] = decals.minimum[1];
                    fragmentParams[18] = decals.minimum[2];
                    fragmentParams[19] = static_cast<float>(std::min<std::uint32_t>(
                        decals.count, static_cast<std::uint32_t>(SDL_MESH_DECAL_LIMIT)));
                    fragmentParams[20] = decals.maximum[0];
                    fragmentParams[21] = decals.maximum[1];
                    fragmentParams[22] = decals.maximum[2];
                    for (std::size_t i = 0; i < SDL_MESH_DECAL_LIMIT; ++i) {
                        const std::size_t offset = 24 + i * 8;
                        std::copy(decals.decals[i].projection.begin(), decals.decals[i].projection.end(),
                                  fragmentParams.begin() + offset);
                        std::copy(decals.decals[i].tint.begin(), decals.decals[i].tint.end(),
                                  fragmentParams.begin() + offset + 4);
                    }
                }
                std::uint32_t materialFlags = 0;
                if (cube.materialPbr) materialFlags |= 1u;
                if (cube.materialRoughnessMap != 0) materialFlags |= 2u;
                if (cube.materialMetallicMap != 0) materialFlags |= 4u;
                if (cube.materialNormalMap != 0) materialFlags |= 8u;
                if (cube.unlit) materialFlags |= 16u;
                fragmentParams[23] = static_cast<float>(materialFlags);
                SDL_PushGPUFragmentUniformData(&commandBuffer, 0, fragmentParams.data(),
                                               sizeof(fragmentParams));
            }
            if (!drawMesh(*pass, cube, error)) {
                return false;
            }
        }
        return true;
    };

    if (shadows) {
        SDL_GPUDepthStencilTargetInfo shadowDepth{};
        shadowDepth.texture = m_shadowTarget;
        shadowDepth.load_op = SDL_GPU_LOADOP_CLEAR;
        shadowDepth.store_op = SDL_GPU_STOREOP_STORE;
        shadowDepth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        shadowDepth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        shadowDepth.clear_depth = 1.0f;
        SDL_GPURenderPass* shadowPass = SDL_BeginGPURenderPass(&commandBuffer, nullptr, 0, &shadowDepth);
        if (!shadowPass) {
            error = sdlError("SDL_BeginGPURenderPass(shadow)");
            return false;
        }
        const SDL_GPUViewport shadowViewport{
            0.0f, 0.0f, static_cast<float>(SHADOW_MAP_SIZE), static_cast<float>(SHADOW_MAP_SIZE),
            0.0f, 1.0f};
        SDL_SetGPUViewport(shadowPass, &shadowViewport);
        const bool shadowDrawn = drawPass(shadowPass, Pass::Shadow, cubes);
        SDL_EndGPURenderPass(shadowPass);
        if (!shadowDrawn) {
            return false;
        }
    }

    const SDL_GPUViewport viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const auto firstBlended = std::ranges::find(cubes, true, &SdlBasicCube::blend);
    const std::size_t opaqueCount = static_cast<std::size_t>(firstBlended - cubes.begin());
    const bool split = !skyFaces.empty() || !celestial.empty();
    const auto opaqueDraws = split ? cubes.first(opaqueCount) : cubes;
    const auto transparentDraws = split ? cubes.subspan(opaqueCount) : std::span<const SdlBasicCube>{};

    auto beginColorDepthPass = [&](SDL_GPULoadOp colorLoad, SDL_GPULoadOp depthLoad) {
        SDL_GPUColorTargetInfo color{};
        color.texture = m_colorTarget;
        color.load_op = colorLoad;
        color.store_op = SDL_GPU_STOREOP_STORE;
        color.clear_color = SDL_FColor{0.05f, 0.07f, 0.10f, 1.0f};
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = m_depthTarget;
        depth.load_op = depthLoad;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.clear_depth = 1.0f;
        SDL_GPURenderPass* result = SDL_BeginGPURenderPass(&commandBuffer, &color, 1, &depth);
        if (result) {
            SDL_SetGPUViewport(result, &viewport);
            SdlMaterialLightingGpu uniforms{};
            uniforms.cameraAndLightCount = lighting.cameraAndLightCount;
            uniforms.sunDirection = lighting.sunDirection;
            uniforms.sunIrradiance = lighting.sunIrradiance;
            uniforms.lights = lighting.lights;
            uniforms.environmentParams = {1.0f, static_cast<float>(ENVIRONMENT_PREFILTER_MIPS - 1),
                                          lighting.shadowDistance, lighting.shadowFadeDistance};
            SDL_PushGPUFragmentUniformData(&commandBuffer, 1, &uniforms, sizeof(uniforms));
        }
        return result;
    };
    auto beginColorPass = [&]() {
        SDL_GPUColorTargetInfo color{};
        color.texture = m_colorTarget;
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* result = SDL_BeginGPURenderPass(&commandBuffer, &color, 1, nullptr);
        if (result) SDL_SetGPUViewport(result, &viewport);
        return result;
    };
    auto drawCelestial = [&](SDL_GPURenderPass& pass, std::span<const SdlCelestialDraw> draws) {
        SDL_GPUTextureSamplerBinding depthBinding{m_depthTarget, m_shadowSampler};
        SDL_GPUGraphicsPipeline* bound = nullptr;
        for (const auto& draw : draws) {
            const auto mode = static_cast<int>(draw.directionAndMode[3]);
            SDL_GPUGraphicsPipeline* pipeline = mode < 2 ? m_celestialDiscPipeline
                                                          : m_celestialAdditivePipeline;
            if (pipeline != bound) {
                SDL_BindGPUGraphicsPipeline(&pass, pipeline);
                SDL_BindGPUVertexSamplers(&pass, 0, &depthBinding, 1);
                SDL_BindGPUFragmentSamplers(&pass, 0, &depthBinding, 1);
                bound = pipeline;
            }
            SDL_PushGPUVertexUniformData(&commandBuffer, 0, &draw, sizeof(draw));
            SDL_PushGPUFragmentUniformData(&commandBuffer, 0, &draw, sizeof(draw));
            SDL_DrawGPUPrimitives(&pass, 6, 1, 0, 0);
        }
    };

    const auto firstFlare = std::ranges::find_if(celestial, [](const SdlCelestialDraw& draw) {
        return draw.directionAndMode[3] >= 2.0f;
    });
    const auto discDraws = celestial.first(static_cast<std::size_t>(firstFlare - celestial.begin()));
    const auto flareDraws = celestial.subspan(static_cast<std::size_t>(firstFlare - celestial.begin()));

    const bool hasSkyboxImage = std::ranges::any_of(skyFaces, [](const SdlBasicCube& face) {
        return face.faceTexture != 0 || face.surfaceTexture != nullptr;
    });
    if (skyboxSource == nullptr || skyFaces.empty() || !hasSkyboxImage) {
        if (m_skyboxPrefilteredCubemap) {
            SDL_ReleaseGPUTexture(m_device, m_skyboxPrefilteredCubemap);
            m_skyboxPrefilteredCubemap = nullptr;
        }
        m_skyboxSourceSignature = std::numeric_limits<std::uint64_t>::max();
        m_skyboxEnvironmentFailureSignature = 0;
    } else if (skyboxSource != nullptr && !skyFaces.empty()) {
        const bool sourceSignatureChanged = skyboxSource->signature != m_skyboxSourceSignature;
        const bool retrySuppressed =
            skyboxSource->signature == m_skyboxEnvironmentFailureSignature;
        const bool updateSource = sourceSignatureChanged && !retrySuppressed;
        const bool updatePrefilter = !retrySuppressed &&
            (sourceSignatureChanged || m_skyboxPrefilteredCubemap == nullptr);
        SDL_GPUTexture* sourceCubemap = updateSource ? nullptr : m_skyboxSourceCubemap;
        SDL_GPUTexture* sourceDepth = updateSource ? nullptr : m_skyboxSourceDepth;
        SDL_GPUTexture* prefilteredCubemap = updatePrefilter ? nullptr : m_skyboxPrefilteredCubemap;
        auto releaseCandidates = [&]() {
            if (updatePrefilter && prefilteredCubemap) {
                SDL_ReleaseGPUTexture(m_device, prefilteredCubemap);
                prefilteredCubemap = nullptr;
            }
            if (updateSource) {
                if (sourceCubemap) {
                    SDL_ReleaseGPUTexture(m_device, sourceCubemap);
                    sourceCubemap = nullptr;
                }
                if (sourceDepth) {
                    SDL_ReleaseGPUTexture(m_device, sourceDepth);
                    sourceDepth = nullptr;
                }
            }
        };
        if (updateSource) {
            sourceCubemap = createSkyboxTarget(*m_device,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                COLOR_FORMAT, error);
            sourceDepth = createSkyboxTarget(*m_device,
                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, DEPTH_FORMAT, error);
            if (!sourceCubemap || !sourceDepth) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "SDL skybox environment source creation failed: %s",
                            error.c_str());
                error.clear();
                releaseCandidates();
                m_skyboxEnvironmentFailureSignature = skyboxSource->signature;
                goto environment_update_done;
            }
        }
        if (updatePrefilter) {
            prefilteredCubemap = createEnvironmentPrefilterTarget(*m_device, error);
            if (!prefilteredCubemap) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "SDL skybox environment prefilter creation failed: %s",
                            error.c_str());
                error.clear();
                releaseCandidates();
                m_skyboxEnvironmentFailureSignature = skyboxSource->signature;
                goto environment_update_done;
            }
        }
        if (updateSource) {
        std::array<SdlBasicCube, 6> sourceFaces{};
        std::copy(skyFaces.begin(), skyFaces.end(), sourceFaces.begin());
        const Matrix4 rotation = [&]() {
            Matrix4 result;
            std::copy_n(skyboxSource->rotation.begin(), 16, result.m);
            return result;
        }();
        const Matrix4 model = rotation * Matrix4::Scale(skyboxSource->size[0],
                                                        skyboxSource->size[1],
                                                        skyboxSource->size[2]);
        Matrix4 depthRange;
        depthRange.m[10] = 0.5f;
        depthRange.m[14] = 0.5f;
        const Matrix4 projection = depthRange * Matrix4::Perspective(90.0f, 1.0f, 0.1f, 10000.0f);
        constexpr std::array<std::array<float, 3>, 6> directions = {{{1, 0, 0}, {-1, 0, 0},
                                                                      {0, 1, 0}, {0, -1, 0},
                                                                      {0, 0, 1}, {0, 0, -1}}};
        constexpr std::array<std::array<float, 3>, 6> ups = {{{0, -1, 0}, {0, -1, 0},
                                                               {0, 0, 1}, {0, 0, -1},
                                                               {0, -1, 0}, {0, -1, 0}}};
        for (unsigned face = 0; face < 6; ++face) {
            const Vector3 direction(directions[face][0], directions[face][1], directions[face][2]);
            const Vector3 up(ups[face][0], ups[face][1], ups[face][2]);
            const Matrix4 view = Matrix4::LookAt(Vector3(0, 0, 0), direction, up);
            const Matrix4 mvp = projection * view * model;
            for (auto& sourceFace : sourceFaces) {
                std::copy_n(mvp.m, 16, sourceFace.modelViewProjection.begin());
                std::copy_n(model.m, 16, sourceFace.modelMatrix.begin());
            }
            SDL_GPUColorTargetInfo color{};
            color.texture = sourceCubemap;
            color.layer_or_depth_plane = face;
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            color.store_op = SDL_GPU_STOREOP_STORE;
            color.clear_color = SDL_FColor{0, 0, 0, 1};
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = sourceDepth;
            depth.layer = static_cast<Uint8>(face);
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.clear_depth = 1.0f;
            SDL_GPURenderPass* sourcePass = SDL_BeginGPURenderPass(&commandBuffer, &color, 1, &depth);
            if (!sourcePass) {
                error = sdlError("SDL_BeginGPURenderPass(skybox source)");
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "SDL skybox environment source pass failed: %s", error.c_str());
                m_skyboxEnvironmentFailureSignature = skyboxSource->signature;
                releaseCandidates();
                return false;
            }
            SDL_GPUViewport sourceViewport{0, 0, static_cast<float>(SKYBOX_SOURCE_SIZE),
                                           static_cast<float>(SKYBOX_SOURCE_SIZE), 0, 1};
            SDL_SetGPUViewport(sourcePass, &sourceViewport);
            const bool sourceDrawn = drawPass(sourcePass, Pass::Sky,
                                              std::span<const SdlBasicCube>(sourceFaces));
            SDL_EndGPURenderPass(sourcePass);
            if (!sourceDrawn) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "SDL skybox environment source draw failed: %s", error.c_str());
                m_skyboxEnvironmentFailureSignature = skyboxSource->signature;
                releaseCandidates();
                return false;
            }
        }
        if (updateSource) {
            SDL_GenerateMipmapsForGPUTexture(&commandBuffer, sourceCubemap);
        }
        }
        if (updatePrefilter) {
        for (unsigned mip = 0; mip < ENVIRONMENT_PREFILTER_MIPS; ++mip) {
            const float roughness = static_cast<float>(mip) /
                                    static_cast<float>(ENVIRONMENT_PREFILTER_MIPS - 1);
            const unsigned targetSize = std::max(1u, ENVIRONMENT_PREFILTER_SIZE >> mip);
            for (unsigned face = 0; face < 6; ++face) {
                SDL_GPUColorTargetInfo color{};
                color.texture = prefilteredCubemap;
                color.mip_level = mip;
                color.layer_or_depth_plane = face;
                color.load_op = SDL_GPU_LOADOP_DONT_CARE;
                color.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* prefilterPass =
                    SDL_BeginGPURenderPass(&commandBuffer, &color, 1, nullptr);
                if (!prefilterPass) {
                    error = sdlError("SDL_BeginGPURenderPass(environment prefilter)");
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "SDL skybox environment prefilter pass failed: %s", error.c_str());
                    m_skyboxEnvironmentFailureSignature = skyboxSource->signature;
                    releaseCandidates();
                    return false;
                }
                const SDL_GPUViewport prefilterViewport{
                    0.0f, 0.0f, static_cast<float>(targetSize), static_cast<float>(targetSize), 0.0f, 1.0f};
                SDL_SetGPUViewport(prefilterPass, &prefilterViewport);
                SDL_BindGPUGraphicsPipeline(prefilterPass, m_environmentPrefilterPipeline);
                SDL_GPUTextureSamplerBinding sourceBinding{sourceCubemap,
                                                            m_environmentPrefilterSampler};
                SDL_BindGPUFragmentSamplers(prefilterPass, 0, &sourceBinding, 1);
                const std::array<float, 4> uniforms{
                    roughness, static_cast<float>(face), static_cast<float>(ENVIRONMENT_PREFILTER_SIZE),
                    static_cast<float>(SKYBOX_SOURCE_SIZE)};
                SDL_PushGPUFragmentUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
                SDL_DrawGPUPrimitives(prefilterPass, 3, 1, 0, 0);
                SDL_EndGPURenderPass(prefilterPass);
            }
        }
        if (updateSource) {
            SDL_ReleaseGPUTexture(m_device, m_skyboxSourceCubemap);
            SDL_ReleaseGPUTexture(m_device, m_skyboxSourceDepth);
            m_skyboxSourceCubemap = sourceCubemap;
            m_skyboxSourceDepth = sourceDepth;
            m_skyboxSourceSignature = skyboxSource->signature;
        }
        if (updatePrefilter) {
            if (m_skyboxPrefilteredCubemap) SDL_ReleaseGPUTexture(m_device, m_skyboxPrefilteredCubemap);
            m_skyboxPrefilteredCubemap = prefilteredCubemap;
        }
        }
    }

environment_update_done:
    SDL_GPURenderPass* pass = beginColorDepthPass(SDL_GPU_LOADOP_CLEAR, SDL_GPU_LOADOP_CLEAR);
    if (!pass) {
        error = sdlError("SDL_BeginGPURenderPass(scene)");
        return false;
    }
    bool mainDrawn = drawPass(pass, Pass::Scene, opaqueDraws);
    if (mainDrawn && !skyFaces.empty()) mainDrawn = drawPass(pass, Pass::Sky, skyFaces);
    if (mainDrawn && discDraws.empty() && clouds.empty() && !transparentDraws.empty()) {
        mainDrawn = drawPass(pass, Pass::Scene, transparentDraws);
    }
    SDL_EndGPURenderPass(pass);
    if (!mainDrawn) return false;

    if (!discDraws.empty()) {
        pass = beginColorPass();
        if (!pass) {
            error = sdlError("SDL_BeginGPURenderPass(celestial disc)");
            return false;
        }
        drawCelestial(*pass, discDraws);
        SDL_EndGPURenderPass(pass);
    }
    if (!clouds.empty()) {
        SDL_GPUColorTargetInfo cloudColor{};
        cloudColor.texture = m_colorTarget;
        cloudColor.load_op = SDL_GPU_LOADOP_LOAD;
        cloudColor.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo cloudDepth{};
        cloudDepth.texture = m_depthTarget;
        cloudDepth.load_op = SDL_GPU_LOADOP_LOAD;
        cloudDepth.store_op = SDL_GPU_STOREOP_STORE;
        cloudDepth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        cloudDepth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* cloudPass = SDL_BeginGPURenderPass(&commandBuffer, &cloudColor, 1, &cloudDepth);
        if (!cloudPass) {
            error = sdlError("SDL_BeginGPURenderPass(cloud)");
            return false;
        }
        SDL_BindGPUGraphicsPipeline(cloudPass, m_cloudPipeline);
        SDL_GPUBufferBinding cloudVertexBinding{m_cloudGeometry.vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(cloudPass, 0, &cloudVertexBinding, 1);
        SDL_GPUTextureSamplerBinding cloudTextureBinding{m_cloudNoiseTexture, m_cloudSampler};
        SDL_BindGPUFragmentSamplers(cloudPass, 0, &cloudTextureBinding, 1);
        for (const auto& cloud : clouds) {
            std::array<float, 48> vertexUniform{};
            std::copy(cloud.view.begin(), cloud.view.end(), vertexUniform.begin());
            std::copy(cloud.projection.begin(), cloud.projection.end(), vertexUniform.begin() + 16);
            std::copy(cloud.model.begin(), cloud.model.end(), vertexUniform.begin() + 32);
            SDL_PushGPUVertexUniformData(&commandBuffer, 0, vertexUniform.data(), sizeof(vertexUniform));
            std::array<float, 8> fragmentUniform = {cloud.color[0], cloud.color[1], cloud.color[2], cloud.color[3],
                                                     cloud.windOffset[0], cloud.windOffset[1], cloud.cover, cloud.density};
            SDL_PushGPUFragmentUniformData(&commandBuffer, 0, fragmentUniform.data(), sizeof(fragmentUniform));
            SDL_DrawGPUPrimitives(cloudPass, m_cloudGeometry.indexCount, 1, 0, 0);
        }
        SDL_EndGPURenderPass(cloudPass);
    }
    if (!transparentDraws.empty() && (!discDraws.empty() || !clouds.empty())) {
        pass = beginColorDepthPass(SDL_GPU_LOADOP_LOAD, SDL_GPU_LOADOP_LOAD);
        if (!pass) {
            error = sdlError("SDL_BeginGPURenderPass(transparent)");
            return false;
        }
        mainDrawn = drawPass(pass, Pass::Scene, transparentDraws);
        SDL_EndGPURenderPass(pass);
        if (!mainDrawn) return false;
    }
    if (!particles.empty()) {
        if (!particleView || !particleProjection) {
            error = "SdlBasicRenderer: particle camera matrices are missing";
            return false;
        }
        if (particles.size() > std::numeric_limits<Uint32>::max() / sizeof(SdlParticleVertex)) {
            error = "SdlBasicRenderer: particle vertex buffer size overflow";
            return false;
        }
        const Uint32 bytes = static_cast<Uint32>(particles.size() * sizeof(SdlParticleVertex));
        if (!m_particleVertexBuffer || m_particleVertexCapacity < bytes) {
            if (m_particleVertexBuffer) SDL_ReleaseGPUBuffer(m_device, m_particleVertexBuffer);
            SDL_GPUBufferCreateInfo info{};
            info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            info.size = bytes;
            m_particleVertexBuffer = SDL_CreateGPUBuffer(m_device, &info);
            m_particleVertexCapacity = m_particleVertexBuffer ? bytes : 0;
            if (!m_particleVertexBuffer) {
                error = sdlError("SDL_CreateGPUBuffer(particles)");
                return false;
            }
        }
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = bytes;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
        if (!transfer) {
            error = sdlError("SDL_CreateGPUTransferBuffer(particles)");
            return false;
        }
        void* mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
        if (!mapped) {
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
            error = sdlError("SDL_MapGPUTransferBuffer(particles)");
            return false;
        }
        std::memcpy(mapped, particles.data(), bytes);
        SDL_UnmapGPUTransferBuffer(m_device, transfer);
        SDL_GPUCommandBuffer* uploadCommand = &commandBuffer;
        SDL_GPUCopyPass* upload = SDL_BeginGPUCopyPass(uploadCommand);
        if (!upload) {
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
            error = sdlError("SDL_BeginGPUCopyPass(particles)");
            return false;
        }
        SDL_GPUTransferBufferLocation source{transfer, 0};
        SDL_GPUBufferRegion destination{m_particleVertexBuffer, 0, bytes};
        SDL_UploadToGPUBuffer(upload, &source, &destination, true);
        SDL_EndGPUCopyPass(upload);
        SDL_GPUColorTargetInfo particleColor{};
        particleColor.texture = m_colorTarget;
        particleColor.load_op = SDL_GPU_LOADOP_LOAD;
        particleColor.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo particleDepth{};
        particleDepth.texture = m_depthTarget;
        particleDepth.load_op = SDL_GPU_LOADOP_LOAD;
        particleDepth.store_op = SDL_GPU_STOREOP_STORE;
        particleDepth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        particleDepth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* particlePass = SDL_BeginGPURenderPass(&commandBuffer, &particleColor, 1, &particleDepth);
        if (!particlePass) {
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
            error = sdlError("SDL_BeginGPURenderPass(particles)");
            return false;
        }
        SDL_BindGPUGraphicsPipeline(particlePass, m_particlePipeline);
        SDL_GPUBufferBinding particleBinding{m_particleVertexBuffer, 0};
        SDL_BindGPUVertexBuffers(particlePass, 0, &particleBinding, 1);
        std::array<float, 32> particleUniforms{};
        std::copy(particleView->begin(), particleView->end(), particleUniforms.begin());
        std::copy(particleProjection->begin(), particleProjection->end(), particleUniforms.begin() + 16);
        SDL_PushGPUVertexUniformData(&commandBuffer, 0, particleUniforms.data(), sizeof(particleUniforms));
        SDL_DrawGPUPrimitives(particlePass, static_cast<Uint32>(particles.size()), 1, 0, 0);
        SDL_EndGPURenderPass(particlePass);
        SDL_ReleaseGPUTransferBuffer(m_device, transfer);
    }
    if (!flareDraws.empty()) {
        pass = beginColorPass();
        if (!pass) {
            error = sdlError("SDL_BeginGPURenderPass(celestial flare)");
            return false;
        }
        drawCelestial(*pass, flareDraws);
        SDL_EndGPURenderPass(pass);
    }
    if (!surfaceMarks.empty()) {
        const auto pushVertex = [&](const SdlBasicCube& cube) {
            std::array<float, 36> uniforms{};
            std::copy(cube.modelViewProjection.begin(), cube.modelViewProjection.end(), uniforms.begin());
            std::copy(cube.modelMatrix.begin(), cube.modelMatrix.end(), uniforms.begin() + 16);
            uniforms[32] = effectTime;
            uniforms[33] = cube.liquid ? 1.0f : 0.0f;
            uniforms[34] = 0.0f;
            SDL_PushGPUVertexUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
        };
        const auto pushMarkVertex = [&](const SdlSurfaceMarkDraw& mark, const SdlBasicCube& cube) {
            Matrix4 markProjection;
            Matrix4 model;
            std::copy(mark.projection.begin(), mark.projection.end(), markProjection.m);
            std::copy(cube.modelMatrix.begin(), cube.modelMatrix.end(), model.m);
            const Matrix4 modelViewProjection = markProjection * model;
            std::array<float, 36> uniforms{};
            std::copy_n(modelViewProjection.m, 16, uniforms.begin());
            std::copy(cube.modelMatrix.begin(), cube.modelMatrix.end(), uniforms.begin() + 16);
            uniforms[32] = effectTime;
            uniforms[33] = cube.liquid ? 1.0f : 0.0f;
            uniforms[34] = 1.0f;
            SDL_PushGPUVertexUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
        };
        const auto pushFragment = [&](const SdlSurfaceMarkDraw& mark, const SdlBasicCube& cube) {
            std::array<float, 40> uniforms{};
            std::copy(mark.worldToLocal.begin(), mark.worldToLocal.end(), uniforms.begin());
            std::copy(mark.projection.begin(), mark.projection.end(), uniforms.begin() + 16);
            std::copy(mark.tint.begin(), mark.tint.end(), uniforms.begin() + 32);
            uniforms[36] = mark.size[0];
            uniforms[37] = mark.size[1];
            uniforms[38] = mark.size[2];
            uniforms[39] = cube.color[3];
            SDL_PushGPUFragmentUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
        };
        for (const auto& mark : surfaceMarks) {
            SDL_GPUDepthStencilTargetInfo depth{};
            depth.texture = m_surfaceMarkDepthTarget;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.clear_depth = 1.0f;
            SDL_GPURenderPass* depthPass =
                SDL_BeginGPURenderPass(&commandBuffer, nullptr, 0, &depth);
            if (!depthPass) {
                error = sdlError("SDL_BeginGPURenderPass(surface mark depth)");
                return false;
            }
            const SDL_GPUViewport surfaceMarkViewport{0, 0,
                static_cast<float>(SURFACE_MARK_MAP_SIZE), static_cast<float>(SURFACE_MARK_MAP_SIZE), 0, 1};
            SDL_SetGPUViewport(depthPass, &surfaceMarkViewport);
            SDL_BindGPUGraphicsPipeline(depthPass, m_surfaceMarkDepthPipeline);
            SDL_GPUTextureSamplerBinding depthBindings[2] = {
                {m_whiteTexture, m_surfaceMarkSampler}, {m_whiteTexture, m_surfaceMarkDepthSampler}};
            SDL_BindGPUFragmentSamplers(depthPass, 0, depthBindings, 2);
            for (const auto& cube : cubes) {
                if (!cube.owner || std::find(mark.targets.begin(), mark.targets.end(), cube.owner) == mark.targets.end()) continue;
                pushMarkVertex(mark, cube);
                if (!drawMesh(*depthPass, cube, error)) { SDL_EndGPURenderPass(depthPass); return false; }
            }
            SDL_EndGPURenderPass(depthPass);
            SDL_GPUColorTargetInfo color{};
            color.texture = m_colorTarget;
            color.load_op = SDL_GPU_LOADOP_LOAD;
            color.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo sceneDepth{};
            sceneDepth.texture = m_depthTarget;
            sceneDepth.load_op = SDL_GPU_LOADOP_LOAD;
            sceneDepth.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* overlayPass =
                SDL_BeginGPURenderPass(&commandBuffer, &color, 1, &sceneDepth);
            if (!overlayPass) {
                error = sdlError("SDL_BeginGPURenderPass(surface mark)");
                return false;
            }
            SDL_SetGPUViewport(overlayPass, &viewport);
            SDL_BindGPUGraphicsPipeline(overlayPass, m_surfaceMarkPipeline);
            const auto markTexture = resolveTexture(mark.texture, error);
            if (!markTexture) { SDL_EndGPURenderPass(overlayPass); return false; }
            SDL_GPUTextureSamplerBinding bindings[2] = {
                {markTexture, m_surfaceMarkSampler}, {m_surfaceMarkDepthTarget, m_surfaceMarkDepthSampler}};
            SDL_BindGPUFragmentSamplers(overlayPass, 0, bindings, 2);
            for (const auto& cube : cubes) {
                if (!cube.owner || std::find(mark.targets.begin(), mark.targets.end(), cube.owner) == mark.targets.end()) continue;
                pushVertex(cube);
                pushFragment(mark, cube);
                if (!drawMesh(*overlayPass, cube, error)) { SDL_EndGPURenderPass(overlayPass); return false; }
            }
            SDL_EndGPURenderPass(overlayPass);
        }
    }
    return true;
}
SDL_GPUTexture* SdlBasicRenderer::colorTarget() const { return m_colorTarget; }

bool SdlBasicRenderer::drawOutline(SDL_GPUCommandBuffer& commandBuffer, SDL_GPUTexture& destination,
                                   std::span<const SdlBasicCube> draws, const Color4& color,
                                   float thickness, std::string& error, float effectTime) {
    error.clear();
    if (!m_device || !m_depthTarget || !m_selectionMaskPipeline || !m_selectionOutlinePipeline) {
        error = "SdlBasicRenderer: outline resources are not initialized";
        return false;
    }
    if (draws.empty()) return true;
    if (!std::isfinite(thickness) || thickness <= 0.0f) {
        error = "SdlBasicRenderer: outline thickness must be positive and finite";
        return false;
    }
    if (!ensureSelectionTarget(m_width, m_height, error)) return false;
    SDL_GPUColorTargetInfo maskColor{};
    maskColor.texture = m_selectionMaskTarget;
    maskColor.load_op = SDL_GPU_LOADOP_CLEAR;
    maskColor.store_op = SDL_GPU_STOREOP_STORE;
    maskColor.clear_color = SDL_FColor{0, 0, 0, 0};
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = m_depthTarget;
    depth.load_op = SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* mask = SDL_BeginGPURenderPass(&commandBuffer, &maskColor, 1, &depth);
    if (!mask) {
        error = sdlError("SDL_BeginGPURenderPass(outline mask)");
        return false;
    }
    SDL_GPUViewport viewport{};
    viewport.w = static_cast<float>(m_width);
    viewport.h = static_cast<float>(m_height);
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(mask, &viewport);
    bool valid = true;
    for (const auto& cube : draws) {
        SDL_BindGPUGraphicsPipeline(mask, cube.doubleSided ? m_selectionMaskDoubleSidedPipeline
                                                            : m_selectionMaskPipeline);
        const auto texture = resolveTexture(cube.texture, error);
        if (!texture) {
            valid = false;
            break;
        }
        SDL_GPUTextureSamplerBinding textureBinding{texture, m_sampler};
        SDL_BindGPUFragmentSamplers(mask, 0, &textureBinding, 1);
        std::array<float, 56> uniforms{};
        std::copy(cube.modelViewProjection.begin(), cube.modelViewProjection.end(), uniforms.begin());
        std::copy(cube.modelMatrix.begin(), cube.modelMatrix.end(), uniforms.begin() + 40);
        uniforms[38] = effectTime;
        uniforms[39] = cube.liquid ? 1.0f : 0.0f;
        SDL_PushGPUVertexUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
        const std::array<float, 4> alphaParams = {
            0.0f, 0.0f, cube.alphaThreshold, cube.useTextureAlpha ? 1.0f : 0.0f};
        SDL_PushGPUFragmentUniformData(&commandBuffer, 0, alphaParams.data(), sizeof(alphaParams));
        if (!drawMesh(*mask, cube, error)) {
            valid = false;
            break;
        }
    }
    SDL_EndGPURenderPass(mask);
    if (!valid) return false;

    SDL_GPUColorTargetInfo target{};
    target.texture = &destination;
    target.load_op = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* outline = SDL_BeginGPURenderPass(&commandBuffer, &target, 1, nullptr);
    if (!outline) {
        error = sdlError("SDL_BeginGPURenderPass(outline)");
        return false;
    }
    SDL_SetGPUViewport(outline, &viewport);
    SDL_BindGPUGraphicsPipeline(outline, m_selectionOutlinePipeline);
    SDL_GPUTextureSamplerBinding binding{m_selectionMaskTarget, m_selectionSampler};
    SDL_BindGPUFragmentSamplers(outline, 0, &binding, 1);
    const std::array<float, 8> uniforms = {
        1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), thickness,
        0.0f, color.r, color.g, color.b, color.a};
    SDL_PushGPUFragmentUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
    SDL_DrawGPUPrimitives(outline, 3, 1, 0, 0);
    SDL_EndGPURenderPass(outline);
    return true;
}

bool SdlBasicRenderer::drawTerrainGuide(SDL_GPUCommandBuffer& commandBuffer, SDL_GPUTexture& destination,
                                        const std::array<float, 16>& viewProjectionFromGuide,
                                        float radius, std::string& error) {
    error.clear();
    if (!m_device || !m_depthTarget || !m_terrainGuidePipeline) {
        error = "SdlBasicRenderer: terrain guide resources are not initialized";
        return false;
    }
    if (!std::isfinite(radius) || radius <= 0.0f) {
        error = "SdlBasicRenderer: terrain guide radius must be positive and finite";
        return false;
    }
    SDL_GPUColorTargetInfo target{};
    target.texture = &destination;
    target.load_op = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = m_depthTarget;
    depth.load_op = SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commandBuffer, &target, 1, &depth);
    if (!pass) {
        error = sdlError("SDL_BeginGPURenderPass(terrain guide)");
        return false;
    }
    SDL_GPUViewport viewport{};
    viewport.w = static_cast<float>(m_width);
    viewport.h = static_cast<float>(m_height);
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);
    SDL_BindGPUGraphicsPipeline(pass, m_terrainGuidePipeline);
    std::array<float, 20> uniforms{};
    std::copy(viewProjectionFromGuide.begin(), viewProjectionFromGuide.end(), uniforms.begin());
    uniforms[16] = radius;
    SDL_PushGPUVertexUniformData(&commandBuffer, 0, uniforms.data(), sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, TERRAIN_GUIDE_VERTEX_COUNT, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    return true;
}
