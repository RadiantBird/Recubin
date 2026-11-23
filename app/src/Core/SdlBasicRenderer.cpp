#include "Core/SdlBasicRenderer.hpp"
#include <algorithm>
#include <array>
#include <basic_gpu_shaders.hpp>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>

namespace {
constexpr SDL_GPUTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
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
    for (auto& g : m_geometries) {
        release(g);
    }
    m_geometries.clear();
    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
    }
    if (m_vertexShader) {
        SDL_ReleaseGPUShader(m_device, m_vertexShader);
    }
    if (m_fragmentShader) {
        SDL_ReleaseGPUShader(m_device, m_fragmentShader);
    }
    if (m_sampler) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
    }
    if (m_colorTarget) {
        SDL_ReleaseGPUTexture(m_device, m_colorTarget);
    }
    if (m_depthTarget) {
        SDL_ReleaseGPUTexture(m_device, m_depthTarget);
    }
    for (auto* texture : m_textures) {
        if (texture) {
            SDL_ReleaseGPUTexture(m_device, texture);
        }
    }
    m_textures.clear();
    m_pipeline = nullptr;
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_sampler = nullptr;
    m_whiteTexture = nullptr;
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
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
    fi.num_samplers = 1;
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
    SDL_GPUColorTargetDescription ct{};
    ct.format = COLOR_FORMAT;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = m_vertexShader;
    pi.fragment_shader = m_fragmentShader;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb;
    pi.vertex_input_state.num_vertex_buffers = 1;
    const std::array<SDL_GPUVertexAttribute, 2> attributes = {va, uvAttribute};
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
    const auto handle = uploadMesh(CUBE_VERTICES, CUBE_INDICES, error);
    if (!handle) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_defaultGeometry = m_geometries.back();
    m_geometries.pop_back();
    const std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
    const auto whiteHandle = uploadTexture(1, 1, white, error);
    if (!whiteHandle) {
        releaseResources();
        m_device = nullptr;
        return false;
    }
    m_whiteTexture = m_textures[whiteHandle - 1];
    return true;
}

std::uint32_t SdlBasicRenderer::uploadMesh(std::span<const SdlBasicVertex> vertices,
                                           std::span<const std::uint32_t> indices, std::string& error) {
    error.clear();
    if (!m_device) {
        error = "SdlBasicRenderer: renderer is not initialized";
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
    m_geometries.push_back(Geometry{vertexBuffer, indexBuffer, static_cast<Uint32>(indices.size())});
    return static_cast<std::uint32_t>(m_geometries.size());
}

std::uint32_t SdlBasicRenderer::uploadTexture(unsigned width, unsigned height,
                                              std::span<const std::uint8_t> rgba,
                                              std::string& error) {
    error.clear();
    if (!m_device) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return 0;
    }
    if (width == 0 || height == 0) {
        error = "SdlBasicRenderer: texture dimensions must be non-zero";
        return 0;
    }
    constexpr std::size_t bytesPerPixel = 4;
    if (width > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
        height > std::numeric_limits<std::size_t>::max() / (width * bytesPerPixel)) {
        error = "SdlBasicRenderer: texture dimensions overflow";
        return 0;
    }
    const std::size_t bytes = static_cast<std::size_t>(width) * height * bytesPerPixel;
    if (rgba.size() != bytes || bytes > std::numeric_limits<Uint32>::max()) {
        error = "SdlBasicRenderer: RGBA texture data has an unexpected size";
        return 0;
    }
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = COLOR_FORMAT;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(m_device, &textureInfo);
    if (!texture) {
        error = sdlError("SDL_CreateGPUTexture(texture)");
        return 0;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(bytes);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
    if (!transfer) {
        SDL_ReleaseGPUTexture(m_device, texture);
        error = sdlError("SDL_CreateGPUTransferBuffer(texture)");
        return 0;
    }
    void* mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(m_device, transfer);
        SDL_ReleaseGPUTexture(m_device, texture);
        error = sdlError("SDL_MapGPUTransferBuffer(texture)");
        return 0;
    }
    std::memcpy(mapped, rgba.data(), bytes);
    SDL_UnmapGPUTransferBuffer(m_device, transfer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(m_device);
    if (!command) {
        SDL_ReleaseGPUTransferBuffer(m_device, transfer);
        SDL_ReleaseGPUTexture(m_device, texture);
        error = sdlError("SDL_AcquireGPUCommandBuffer(texture)");
        return 0;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(m_device, transfer);
        SDL_ReleaseGPUTexture(m_device, texture);
        error = sdlError("SDL_BeginGPUCopyPass(texture)");
        return 0;
    }
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_GPUTextureRegion destination{texture, 0, 0, 0, 0, 0, width, height, 1};
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    const bool uploaded = fence != nullptr && SDL_WaitForGPUFences(m_device, true, &fence, 1);
    if (fence) {
        SDL_ReleaseGPUFence(m_device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(m_device, transfer);
    if (!uploaded) {
        SDL_ReleaseGPUTexture(m_device, texture);
        error = sdlError("SDL_SubmitGPUCommandBufferAndAcquireFence(texture)");
        return 0;
    }
    m_textures.push_back(texture);
    return static_cast<std::uint32_t>(m_textures.size());
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

bool SdlBasicRenderer::draw(SDL_GPUCommandBuffer& commandBuffer, unsigned width, unsigned height,
                            std::span<const SdlBasicCube> cubes, std::string& error) {
    error.clear();
    if (!m_device || !m_pipeline || !m_defaultGeometry.vertexBuffer) {
        error = "SdlBasicRenderer: renderer is not initialized";
        return false;
    }
    if (!ensureTargets(width, height, error)) {
        return false;
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
    SDL_GPUViewport vp{};
    vp.w = static_cast<float>(width);
    vp.h = static_cast<float>(height);
    vp.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &vp);
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    for (const auto& cube : cubes) {
        const Geometry* g = &m_defaultGeometry;
        if (cube.geometry) {
            if (cube.geometry > m_geometries.size()) {
                SDL_EndGPURenderPass(pass);
                error = "SdlBasicRenderer: invalid geometry handle";
                return false;
            }
            g = &m_geometries[cube.geometry - 1];
        }
        SDL_GPUBufferBinding vb{g->vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{g->indexBuffer, 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        const std::uint32_t firstIndex = cube.firstIndex;
        if (firstIndex >= g->indexCount || firstIndex % 3 != 0) {
            SDL_EndGPURenderPass(pass);
            error = "SdlBasicRenderer: draw does not start at a triangle in the geometry";
            return false;
        }
        const std::uint32_t remainingIndices = g->indexCount - firstIndex;
        const std::uint32_t indexCount = cube.indexCount == 0 ? remainingIndices : cube.indexCount;
        if (indexCount > remainingIndices || indexCount % 3 != 0) {
            SDL_EndGPURenderPass(pass);
            error = "SdlBasicRenderer: draw index range is outside geometry";
            return false;
        }
        SDL_GPUTexture* texture = m_whiteTexture;
        if (cube.texture != 0) {
            if (cube.texture > m_textures.size()) {
                SDL_EndGPURenderPass(pass);
                error = "SdlBasicRenderer: invalid texture handle";
                return false;
            }
            texture = m_textures[cube.texture - 1];
        }
        SDL_GPUTextureSamplerBinding samplerBinding{texture, m_sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);
        std::array<float, 20> u{};
        std::copy(cube.modelViewProjection.begin(), cube.modelViewProjection.end(), u.begin());
        std::copy(cube.color.begin(), cube.color.end(), u.begin() + cube.modelViewProjection.size());
        SDL_PushGPUVertexUniformData(&commandBuffer, 0, u.data(), sizeof(u));
        SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, firstIndex, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
    return true;
}
SDL_GPUTexture* SdlBasicRenderer::colorTarget() const { return m_colorTarget; }
