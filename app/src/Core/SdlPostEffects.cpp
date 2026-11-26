#include <Core/SdlPostEffects.hpp>

#include <Core/PostEffectSpirv.hpp>
#include <Core/SdlPostEffectDxil.hpp>
#include <Core/SdlPostEffectMsl.hpp>
#include <Instances/PostEffect.hpp>
#include <Instances/Workspace.hpp>
#include <Util/AssetPath.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/Logger.hpp>
#include <post_effect_gpu_shaders.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace {
constexpr SDL_GPUTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

void flipTexture(SDL_GPUCommandBuffer& command, SDL_GPUTexture* source, SDL_GPUTexture* destination,
                 unsigned width, unsigned height) {
    SDL_GPUBlitInfo blit{};
    blit.source = {source, 0, 0, 0, 0, width, height};
    blit.destination = {destination, 0, 0, 0, 0, width, height};
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.flip_mode = SDL_FLIP_VERTICAL;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(&command, &blit);
}

std::string sdlError(const char* operation) {
    std::string result(operation);
    const char* detail = SDL_GetError();
    if (detail && *detail) {
        result += ": ";
        result += detail;
    }
    return result;
}

std::string readFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open " + path.string();
        return {};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        error = "cannot read " + path.string();
        return {};
    }
    return contents.str();
}
}

struct SdlPostEffects::EffectState {
    std::filesystem::file_time_type stamp{};
    bool hasStamp = false;
    bool failureReported = false;
    SDL_GPUShader* fragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};

SdlPostEffects::SdlPostEffects() = default;
SdlPostEffects::~SdlPostEffects() { releaseResources(); }

void SdlPostEffects::releaseResources() {
    if (!m_device) return;
    SDL_WaitForGPUIdle(m_device);
    for (auto& [path, state] : m_effects) {
        if (state->pipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, state->pipeline);
        if (state->fragmentShader) SDL_ReleaseGPUShader(m_device, state->fragmentShader);
    }
    if (m_builtinPipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_builtinPipeline);
    if (m_builtinFragmentShader) SDL_ReleaseGPUShader(m_device, m_builtinFragmentShader);
    m_effects.clear();
    for (SDL_GPUTexture*& texture : m_pingPong) {
        if (texture) SDL_ReleaseGPUTexture(m_device, texture);
        texture = nullptr;
    }
    if (m_sampler) SDL_ReleaseGPUSampler(m_device, m_sampler);
    if (m_vertexShader) SDL_ReleaseGPUShader(m_device, m_vertexShader);
    m_sampler = nullptr;
    m_vertexShader = nullptr;
    m_builtinFragmentShader = nullptr;
    m_builtinPipeline = nullptr;
    m_width = 0;
    m_height = 0;
}

bool SdlPostEffects::initialize(SDL_GPUDevice& device, std::string& error) {
    error.clear();
    releaseResources();
    m_device = &device;
    const char* driver = SDL_GetGPUDeviceDriver(&device);
    if (!driver || (std::string_view(driver) != "vulkan" && std::string_view(driver) != "direct3d12" &&
                    std::string_view(driver) != "metal")) {
        error = "SdlPostEffects: unsupported SDL GPU driver";
        releaseResources();
        m_device = nullptr;
        return false;
    }

    SDL_GPUShaderCreateInfo vertexInfo{};
    const bool direct3d12 = std::string_view(driver) == "direct3d12";
    const bool metal = std::string_view(driver) == "metal";
    vertexInfo.code = direct3d12 ? post_effect_gpu_vertex_dxil : (metal ? post_effect_gpu_vertex_msl : post_effect_gpu_vertex_spirv);
    vertexInfo.code_size = direct3d12 ? sizeof(post_effect_gpu_vertex_dxil) : (metal ? sizeof(post_effect_gpu_vertex_msl) : sizeof(post_effect_gpu_vertex_spirv));
    vertexInfo.entrypoint = "VSMain";
    vertexInfo.format = direct3d12 ? SDL_GPU_SHADERFORMAT_DXIL : (metal ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV);
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    m_vertexShader = SDL_CreateGPUShader(&device, &vertexInfo);
    if (!m_vertexShader) {
        error = sdlError("SDL_CreateGPUShader(post effect vertex)");
        releaseResources();
        m_device = nullptr;
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_sampler = SDL_CreateGPUSampler(&device, &samplerInfo);
    if (!m_sampler) {
        error = sdlError("SDL_CreateGPUSampler(post effect)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code = direct3d12 ? post_effect_gpu_fragment_dxil : (metal ? post_effect_gpu_fragment_msl : post_effect_gpu_fragment_spirv);
    fragmentInfo.code_size = direct3d12 ? sizeof(post_effect_gpu_fragment_dxil) : (metal ? sizeof(post_effect_gpu_fragment_msl) : sizeof(post_effect_gpu_fragment_spirv));
    fragmentInfo.entrypoint = "PSMain";
    fragmentInfo.format = direct3d12 ? SDL_GPU_SHADERFORMAT_DXIL : (metal ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV);
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 1;
    fragmentInfo.num_uniform_buffers = 1;
    m_builtinFragmentShader = SDL_CreateGPUShader(&device, &fragmentInfo);
    if (!m_builtinFragmentShader) {
        error = sdlError("SDL_CreateGPUShader(builtin post effect fragment)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    SDL_GPUColorTargetDescription target{};
    target.format = COLOR_FORMAT;
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = m_vertexShader;
    pipelineInfo.fragment_shader = m_builtinFragmentShader;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.rasterizer_state.enable_depth_clip = true;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = &target;
    m_builtinPipeline = SDL_CreateGPUGraphicsPipeline(&device, &pipelineInfo);
    if (!m_builtinPipeline) {
        error = sdlError("SDL_CreateGPUGraphicsPipeline(builtin post effect)");
        releaseResources();
        m_device = nullptr;
        return false;
    }
    return true;
}

bool SdlPostEffects::ensureTargets(unsigned width, unsigned height, std::string& error) {
    if (!width || !height) {
        error = "SdlPostEffects: render target dimensions must be non-zero";
        return false;
    }
    if (m_pingPong[0] && m_pingPong[1] && m_width == width && m_height == height) return true;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = COLOR_FORMAT;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* first = SDL_CreateGPUTexture(m_device, &info);
    SDL_GPUTexture* second = SDL_CreateGPUTexture(m_device, &info);
    if (!first || !second) {
        if (first) SDL_ReleaseGPUTexture(m_device, first);
        if (second) SDL_ReleaseGPUTexture(m_device, second);
        error = sdlError("SDL_CreateGPUTexture(post effect target)");
        return false;
    }
    if (m_pingPong[0]) SDL_ReleaseGPUTexture(m_device, m_pingPong[0]);
    if (m_pingPong[1]) SDL_ReleaseGPUTexture(m_device, m_pingPong[1]);
    m_pingPong[0] = first;
    m_pingPong[1] = second;
    m_width = width;
    m_height = height;
    return true;
}

bool SdlPostEffects::refreshEffect(EffectState& state, const std::string& path, std::string& error) {
    if (!AssetGuard::allow(path)) {
        error = path + ": shader path is not allowed";
        return false;
    }
    const std::filesystem::path file = AssetPath::fromStored(path);
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(file, ec);
    if (ec) {
        error = path + ": cannot stat shader file";
        return false;
    }
    if (state.hasStamp && state.stamp == stamp) return state.pipeline != nullptr;
    state.stamp = stamp;
    state.hasStamp = true;
    state.failureReported = false;
    std::string readError;
    const std::string source = readFile(file, readError);
    if (!readError.empty()) {
        error = path + ": " + readError;
        return false;
    }
    const auto compiled = PostEffectSpirv::compile(source, path);
    if (!compiled.success) {
        error = compiled.diagnostic.message;
        return false;
    }
    std::vector<std::uint8_t> dxil;
    std::string msl;
    std::string entryPoint = "main";
    const std::string_view gpuDriver = SDL_GetGPUDeviceDriver(m_device);
    const bool direct3d12 = gpuDriver == "direct3d12";
    const bool metal = gpuDriver == "metal";
    if (direct3d12) {
        std::string conversionError;
        if (!compileSdlPostEffectDxil(compiled.spirv, dxil, conversionError)) {
            error = path + ": " + conversionError;
            return false;
        }
    } else if (metal) {
        std::string conversionError;
        if (!compileSdlPostEffectMsl(compiled.spirv, msl, entryPoint, conversionError)) {
            error = path + ": " + conversionError;
            return false;
        }
    }
    SDL_GPUShaderCreateInfo info{};
    info.code = direct3d12
                    ? reinterpret_cast<const Uint8*>(dxil.data())
                    : (metal ? reinterpret_cast<const Uint8*>(msl.data())
                             : reinterpret_cast<const Uint8*>(compiled.spirv.data()));
    info.code_size = direct3d12
                         ? dxil.size() : (metal ? msl.size() : compiled.spirv.size() * sizeof(std::uint32_t));
    info.entrypoint = entryPoint.c_str();
    info.format = direct3d12
                      ? SDL_GPU_SHADERFORMAT_DXIL : (metal ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV);
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1;
    info.num_uniform_buffers = 1;
    SDL_GPUShader* shader = SDL_CreateGPUShader(m_device, &info);
    if (!shader) {
        error = path + ": " + sdlError("SDL_CreateGPUShader(custom fragment)");
        return false;
    }
    SDL_GPUColorTargetDescription target{};
    target.format = COLOR_FORMAT;
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = m_vertexShader;
    pipelineInfo.fragment_shader = shader;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.rasterizer_state.enable_depth_clip = true;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = &target;
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipelineInfo);
    if (!pipeline) {
        SDL_ReleaseGPUShader(m_device, shader);
        error = path + ": " + sdlError("SDL_CreateGPUGraphicsPipeline(custom)");
        return false;
    }
    if (state.pipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, state.pipeline);
    if (state.fragmentShader) SDL_ReleaseGPUShader(m_device, state.fragmentShader);
    state.fragmentShader = shader;
    state.pipeline = pipeline;
    state.failureReported = false;
    return true;
}

bool SdlPostEffects::apply(Workspace& workspace, SDL_GPUCommandBuffer& commandBuffer,
                           SDL_GPUTexture& input, unsigned width, unsigned height,
                           float elapsed, SDL_GPUTexture*& output, std::string& error) {
    error.clear();
    output = &input;
    if (!m_device || !m_vertexShader || !m_sampler) {
        error = "SdlPostEffects: renderer is not initialized";
        return false;
    }
    std::vector<PostEffect*> effects;
    for (PostEffect* effect : workspace.getRenderPostEffects()) {
        if (effect && effect->Enabled && effect->Type != PostEffectKind::None) effects.push_back(effect);
    }
    std::stable_sort(effects.begin(), effects.end(), [](const PostEffect* a, const PostEffect* b) {
        return a->ZIndex < b->ZIndex;
    });
    SDL_GPUTexture* source = &input;
    unsigned targetIndex = 0;
    for (PostEffect* effect : effects) {
        SDL_GPUGraphicsPipeline* pipeline = m_builtinPipeline;
        if (effect->Type == PostEffectKind::Custom) {
            const std::string path = AssetPath::normalize(effect->FragmentShaderFile);
            if (path.empty()) continue;
            auto& state = m_effects[path];
            if (!state) state = std::make_unique<EffectState>();
            std::string refreshError;
            if (!refreshEffect(*state, path, refreshError)) {
                if (!state->failureReported) {
                    RCBN_WARN("SDL PostEffect " << refreshError <<
                              (state->pipeline ? " (retaining last successful shader)" : " (effect bypassed)"));
                    state->failureReported = true;
                }
                if (!state->pipeline) continue;
            }
            pipeline = state->pipeline;
        }
        // Both public coordinates and scratch texture rows use the GLSL lower-left origin.
        // Convert once on entry to a contiguous group of custom effects.
        if (source == &input) {
            if (!ensureTargets(width, height, error)) return false;
            flipTexture(commandBuffer, &input, m_pingPong[0], width, height);
            source = m_pingPong[0];
            targetIndex = 1;
        }
        SDL_GPUColorTargetInfo target{};
        target.texture = m_pingPong[targetIndex];
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commandBuffer, &target, 1, nullptr);
        if (!pass) {
            error = sdlError("SDL_BeginGPURenderPass(post effect)");
            return false;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_GPUTextureSamplerBinding binding{source, m_sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        std::array<float, 8> params{};
        params[0] = static_cast<float>(width);
        params[1] = static_cast<float>(height);
        params[2] = elapsed;
        params[3] = effect->Intensity;
        params[4] = effect->Param1;
        params[5] = effect->Param2;
        params[6] = static_cast<float>(static_cast<int>(effect->Type));
        params[7] = elapsed;
        SDL_PushGPUFragmentUniformData(&commandBuffer, 0, params.data(), sizeof(params));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        source = m_pingPong[targetIndex];
        targetIndex ^= 1u;
    }
    if (source != &input) {
        flipTexture(commandBuffer, source, m_pingPong[targetIndex], width, height);
        output = m_pingPong[targetIndex];
    }
    return true;
}
