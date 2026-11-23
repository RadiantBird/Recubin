#include "Core/SdlBasicRenderer.hpp"
#include <algorithm>
#include <array>
#include <basic_gpu_shaders.hpp>
#include <selection_mask_gpu_shaders.hpp>
#include <selection_outline_gpu_shaders.hpp>
#include <post_effect_gpu_shaders.hpp>
#include <shadow_gpu_shaders.hpp>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>

namespace {
constexpr SDL_GPUTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr unsigned SHADOW_MAP_SIZE = 2048;
std::string sdlError(std::string_view operation) {
    std::string message(operation);
    const char* detail = SDL_GetError();
    if (detail != nullptr && detail[0] != '\0') {
        message += ": ";
        message += detail;
    }
    return message;
}
constexpr std::array<SdlBasicVertex, 8> CUBE_VERTICES = {{{{{-0.5f, -0.5f, -0.5f}}, {}},
                                                           {{{0.5f, -0.5f, -0.5f}}, {}},
                                                           {{{0.5f, 0.5f, -0.5f}}, {}},
                                                           {{{-0.5f, 0.5f, -0.5f}}, {}},
                                                           {{{-0.5f, -0.5f, 0.5f}}, {}},
                                                           {{{0.5f, -0.5f, 0.5f}}, {}},
                                                           {{{0.5f, 0.5f, 0.5f}}, {}},
                                                           {{{-0.5f, 0.5f, 0.5f}}, {}}}};
constexpr std::array<std::uint32_t, 36> CUBE_INDICES = {{4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2,
                                                         0, 4, 7, 0, 7, 3, 5, 1, 2, 5, 2, 6,
                                                         3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4}};
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
    if (m_sampler) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
    }
    if (m_shadowSampler) {
        SDL_ReleaseGPUSampler(m_device, m_shadowSampler);
    }
    if (m_selectionSampler) {
        SDL_ReleaseGPUSampler(m_device, m_selectionSampler);
    }
    if (m_colorTarget) {
        SDL_ReleaseGPUTexture(m_device, m_colorTarget);
    }
    if (m_depthTarget) {
        SDL_ReleaseGPUTexture(m_device, m_depthTarget);
    }
    if (m_shadowTarget) {
        SDL_ReleaseGPUTexture(m_device, m_shadowTarget);
    }
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
    m_shadowPipeline = nullptr;
    m_selectionMaskPipeline = nullptr;
    m_selectionMaskDoubleSidedPipeline = nullptr;
    m_selectionOutlinePipeline = nullptr;
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_shadowFragmentShader = nullptr;
    m_selectionMaskFragmentShader = nullptr;
    m_selectionOutlineVertexShader = nullptr;
    m_selectionOutlineFragmentShader = nullptr;
    m_sampler = nullptr;
    m_shadowSampler = nullptr;
    m_selectionSampler = nullptr;
    m_whiteTexture = nullptr;
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
    m_shadowTarget = nullptr;
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
    fi.num_samplers = 2;
    fi.num_uniform_buffers = 1;
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
    SDL_GPUColorTargetDescription ct{};
    ct.format = COLOR_FORMAT;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = m_vertexShader;
    pi.fragment_shader = m_fragmentShader;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb;
    pi.vertex_input_state.num_vertex_buffers = 1;
    const std::array<SDL_GPUVertexAttribute, 3> attributes = {va, uvAttribute, colorAttribute};
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
    SDL_GPUGraphicsPipelineCreateInfo shadowPi = pi;
    SDL_GPUShaderCreateInfo shadowShaderInfo{};
    shadowShaderInfo.format = fi.format;
    shadowShaderInfo.entrypoint = "PSMain";
    shadowShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
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
    if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
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
    if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
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
    if (fi.format == SDL_GPU_SHADERFORMAT_DXIL) {
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
    const auto handle = uploadMesh(CUBE_VERTICES, CUBE_INDICES, error);
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
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
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

bool SdlBasicRenderer::draw(SDL_GPUCommandBuffer& commandBuffer, unsigned width, unsigned height,
                            std::span<const SdlBasicCube> cubes, std::string& error, bool shadows) {
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
    enum class Pass { Scene, Shadow, Selection };
    const auto drawPass = [&](SDL_GPURenderPass* pass, Pass kind) -> bool {
        const bool shadowPass = kind == Pass::Shadow;
        const bool selectionPass = kind == Pass::Selection;
        SDL_GPUViewport vp{};
        vp.w = shadowPass ? static_cast<float>(SHADOW_MAP_SIZE) : static_cast<float>(width);
        vp.h = shadowPass ? static_cast<float>(SHADOW_MAP_SIZE) : static_cast<float>(height);
        vp.max_depth = 1.0f;
        SDL_SetGPUViewport(pass, &vp);
        SDL_GPUGraphicsPipeline* pipeline = m_pipeline;
        SDL_GPUGraphicsPipeline* doubleSidedPipeline = m_doubleSidedPipeline;
        if (shadowPass) {
            pipeline = m_shadowPipeline;
        } else if (selectionPass) {
            pipeline = m_selectionMaskPipeline;
            doubleSidedPipeline = m_selectionMaskDoubleSidedPipeline;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        bool boundDoubleSided = false;
        for (const auto& cube : cubes) {
            if ((shadowPass && !cube.castShadow) || (selectionPass && !cube.selected)) {
                continue;
            }
            if (!shadowPass && cube.doubleSided != boundDoubleSided) {
                SDL_BindGPUGraphicsPipeline(pass, cube.doubleSided ? doubleSidedPipeline : pipeline);
                boundDoubleSided = cube.doubleSided;
            }
            const Geometry* g = &m_defaultGeometry;
            if (cube.geometry) {
                const auto geometry = m_geometries.find(cube.geometry);
                if (geometry == m_geometries.end()) {
                    error = "SdlBasicRenderer: invalid geometry handle";
                    return false;
                }
                g = &geometry->second;
            }
            SDL_GPUBufferBinding vb{g->vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_GPUBufferBinding ib{g->indexBuffer, 0};
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const std::uint32_t firstIndex = cube.firstIndex;
            if (firstIndex >= g->indexCount || firstIndex % 3 != 0) {
                error = "SdlBasicRenderer: draw does not start at a triangle in the geometry";
                return false;
            }
            const std::uint32_t remainingIndices = g->indexCount - firstIndex;
            const std::uint32_t indexCount = cube.indexCount == 0 ? remainingIndices : cube.indexCount;
            if (indexCount > remainingIndices || indexCount % 3 != 0) {
                error = "SdlBasicRenderer: draw index range is outside geometry";
                return false;
            }
            if (kind == Pass::Scene) {
                SDL_GPUTexture* texture = m_whiteTexture;
                if (cube.texture != 0) {
                    const auto it = m_textures.find(cube.texture);
                    if (it == m_textures.end()) {
                        error = "SdlBasicRenderer: invalid texture handle";
                        return false;
                    }
                    texture = it->second;
                }
                const std::array<SDL_GPUTextureSamplerBinding, 2> samplerBindings = {
                    SDL_GPUTextureSamplerBinding{texture, m_sampler},
                    SDL_GPUTextureSamplerBinding{shadows ? m_shadowTarget : m_whiteTexture, m_shadowSampler}};
                SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings.data(),
                                            static_cast<Uint32>(samplerBindings.size()));
            }
            std::array<float, 40> u{};
            std::copy(cube.modelViewProjection.begin(), cube.modelViewProjection.end(), u.begin());
            std::copy(cube.shadowModelViewProjection.begin(), cube.shadowModelViewProjection.end(),
                      u.begin() + 16);
            std::copy(cube.color.begin(), cube.color.end(), u.begin() + 32);
            u[36] = shadowPass ? 1.0f : 0.0f;
            SDL_PushGPUVertexUniformData(&commandBuffer, 0, u.data(), sizeof(u));
            const std::array<float, 4> shadowParams = {
                (shadows && cube.receiveShadow) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            if (kind == Pass::Scene) {
                SDL_PushGPUFragmentUniformData(&commandBuffer, 0, shadowParams.data(),
                                               sizeof(shadowParams));
            }
            SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, firstIndex, 0, 0);
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
        const bool shadowDrawn = drawPass(shadowPass, Pass::Shadow);
        SDL_EndGPURenderPass(shadowPass);
        if (!shadowDrawn) {
            return false;
        }
    }

    SDL_GPUColorTargetInfo ci{};
    ci.texture = m_colorTarget;
    ci.load_op = SDL_GPU_LOADOP_CLEAR;
    ci.store_op = SDL_GPU_STOREOP_STORE;
    ci.clear_color = SDL_FColor{0.05f, 0.07f, 0.10f, 1.0f};
    SDL_GPUDepthStencilTargetInfo di{};
    di.texture = m_depthTarget;
    di.load_op = SDL_GPU_LOADOP_CLEAR;
    di.store_op = SDL_GPU_STOREOP_STORE;
    di.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    di.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    di.clear_depth = 1.0f;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commandBuffer, &ci, 1, &di);
    if (!pass) {
        error = sdlError("SDL_BeginGPURenderPass");
        return false;
    }
    const bool mainDrawn = drawPass(pass, Pass::Scene);
    SDL_EndGPURenderPass(pass);
    if (!mainDrawn) {
        return false;
    }
    if (std::ranges::none_of(cubes, &SdlBasicCube::selected)) {
        return true;
    }
    if (!ensureSelectionTarget(width, height, error)) {
        return false;
    }

    SDL_GPUColorTargetInfo maskColor{};
    maskColor.texture = m_selectionMaskTarget;
    maskColor.load_op = SDL_GPU_LOADOP_CLEAR;
    maskColor.store_op = SDL_GPU_STOREOP_STORE;
    maskColor.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f};
    SDL_GPUDepthStencilTargetInfo maskDepth{};
    maskDepth.texture = m_depthTarget;
    maskDepth.load_op = SDL_GPU_LOADOP_LOAD;
    maskDepth.store_op = SDL_GPU_STOREOP_STORE;
    maskDepth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    maskDepth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* maskPass = SDL_BeginGPURenderPass(&commandBuffer, &maskColor, 1, &maskDepth);
    if (!maskPass) {
        error = sdlError("SDL_BeginGPURenderPass(selection mask)");
        return false;
    }
    const bool maskDrawn = drawPass(maskPass, Pass::Selection);
    SDL_EndGPURenderPass(maskPass);
    if (!maskDrawn) {
        return false;
    }

    SDL_GPUColorTargetInfo outlineColor{};
    outlineColor.texture = m_colorTarget;
    outlineColor.load_op = SDL_GPU_LOADOP_LOAD;
    outlineColor.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* outlinePass = SDL_BeginGPURenderPass(&commandBuffer, &outlineColor, 1, nullptr);
    if (!outlinePass) {
        error = sdlError("SDL_BeginGPURenderPass(selection outline)");
        return false;
    }
    SDL_BindGPUGraphicsPipeline(outlinePass, m_selectionOutlinePipeline);
    SDL_GPUTextureSamplerBinding outlineBinding{m_selectionMaskTarget, m_selectionSampler};
    SDL_BindGPUFragmentSamplers(outlinePass, 0, &outlineBinding, 1);
    const std::array<float, 4> texelSize = {1.0f / width, 1.0f / height, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(&commandBuffer, 0, texelSize.data(), sizeof(texelSize));
    SDL_DrawGPUPrimitives(outlinePass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(outlinePass);
    return true;
}
SDL_GPUTexture* SdlBasicRenderer::colorTarget() const { return m_colorTarget; }
