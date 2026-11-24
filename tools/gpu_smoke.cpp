#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <Core/SdlGpuWindow.hpp>
#include <Core/SDLInputBackend.hpp>
#include <Util/PngWriter.hpp>
#include <gpu_smoke_shaders.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr Uint32 WIDTH = 256;
constexpr Uint32 HEIGHT = 128;
constexpr Uint32 TEXTURE_OFFSET = 512;
constexpr Uint32 TEXTURE_ROW_BYTES = 256;
constexpr SDL_GPUTextureFormat COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

struct Vertex {
    float position[3];
    float uv[2];
};

constexpr std::array<Vertex, 6> quad(float z, float leftU, float rightU, float topV, float bottomV) {
    const Vertex topLeft{{-1.0f, 1.0f, z}, {leftU, topV}};
    const Vertex topRight{{1.0f, 1.0f, z}, {rightU, topV}};
    const Vertex bottomLeft{{-1.0f, -1.0f, z}, {leftU, bottomV}};
    const Vertex bottomRight{{1.0f, -1.0f, z}, {rightU, bottomV}};
    return {topLeft, topRight, bottomLeft, bottomLeft, topRight, bottomRight};
}

template <typename T>
T require(T result, std::string_view operation) {
    if (!result) {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
    return result;
}

struct Smoke {
    SdlGpuWindow host;
    // Borrowed handles owned by host. They remain valid until all resources
    // below have been released and host.shutdown() is called.
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* scenePipeline = nullptr;
    SDL_GPUGraphicsPipeline* compositePipeline = nullptr;
    SDL_GPUTexture* source = nullptr;
    SDL_GPUTexture* scene = nullptr;
    SDL_GPUTexture* depth = nullptr;
    SDL_GPUTexture* finalColor = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUTransferBuffer* upload = nullptr;
    SDL_GPUTransferBuffer* download = nullptr;
    SDL_GPUFence* fence = nullptr;

    ~Smoke() {
        if (device) {
            SDL_WaitForGPUIdle(device);
            if (fence) SDL_ReleaseGPUFence(device, fence);
            if (download) SDL_ReleaseGPUTransferBuffer(device, download);
            if (upload) SDL_ReleaseGPUTransferBuffer(device, upload);
            if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
            if (sampler) SDL_ReleaseGPUSampler(device, sampler);
            if (finalColor) SDL_ReleaseGPUTexture(device, finalColor);
            if (depth) SDL_ReleaseGPUTexture(device, depth);
            if (scene) SDL_ReleaseGPUTexture(device, scene);
            if (source) SDL_ReleaseGPUTexture(device, source);
            if (compositePipeline) SDL_ReleaseGPUGraphicsPipeline(device, compositePipeline);
            if (scenePipeline) SDL_ReleaseGPUGraphicsPipeline(device, scenePipeline);
            if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
            if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
        }
        host.shutdown();
    }

    SDL_GPUTexture* texture(Uint32 w, Uint32 h, SDL_GPUTextureFormat format,
                            SDL_GPUTextureUsageFlags usage, const char* label) {
        require(SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, usage), label);
        SDL_GPUTextureCreateInfo info{};
        info.format = format;
        info.usage = usage;
        info.width = w;
        info.height = h;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        return require(SDL_CreateGPUTexture(device, &info), label);
    }

    template <typename Bytes>
    SDL_GPUShader* shader(const Bytes& code, SDL_GPUShaderStage stage,
                          SDL_GPUShaderFormat format, const char* entry) {
        const auto bytes = std::as_bytes(std::span{code});
        SDL_GPUShaderCreateInfo info{};
        info.code = reinterpret_cast<const Uint8*>(bytes.data());
        info.code_size = bytes.size();
        info.entrypoint = entry;
        info.format = format;
        info.stage = stage;
        info.num_samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1 : 0;
        return require(SDL_CreateGPUShader(device, &info), entry);
    }

    SDL_GPUGraphicsPipeline* pipeline(bool withDepth) {
        const SDL_GPUVertexBufferDescription buffer{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        const SDL_GPUVertexAttribute attributes[]{
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, uv)}
        };
        SDL_GPUColorTargetDescription color{};
        color.format = COLOR_FORMAT;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state = {&buffer, 1, attributes, 2};
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.enable_depth_clip = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        info.depth_stencil_state.enable_depth_test = withDepth;
        info.depth_stencil_state.enable_depth_write = withDepth;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = withDepth;
        info.target_info.depth_stencil_format = DEPTH_FORMAT;
        return require(SDL_CreateGPUGraphicsPipeline(device, &info), "create test graphics pipeline");
    }

    void initialize(const std::string& driver, bool offscreen) {
        SdlGpuWindowConfig config;
        config.title = "Recubin GPU smoke";
        config.width = WIDTH;
        config.height = HEIGHT;
        config.present = !offscreen;
        config.driver = driver == "vulkan" ? SdlGpuDriver::Vulkan :
                        driver == "direct3d12" ? SdlGpuDriver::Direct3D12 : SdlGpuDriver::Auto;
        config.shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
        std::string error;
        if (config.present) {
            auto invalid = config;
            invalid.driver = SdlGpuDriver::Metal;
            invalid.shaderFormats = SDL_GPU_SHADERFORMAT_MSL;
            if (host.initialize(invalid, &error) || host.device() || host.window() ||
                error.find("create GPU device (driver=metal)") == std::string::npos) {
                throw std::runtime_error("Unavailable SDL GPU driver did not roll back initialization: " + error);
            }
        }
        if (!host.initialize(config, &error)) {
            throw std::runtime_error(error);
        }
        device = host.device();
        window = host.window();
        const std::string_view actual = host.driverName();
        std::cout << "GPU driver: " << actual << '\n';
        if (actual == "vulkan") {
            vertexShader = shader(gpu_smoke_vertex_spirv, SDL_GPU_SHADERSTAGE_VERTEX,
                                  SDL_GPU_SHADERFORMAT_SPIRV, "VSMain");
            fragmentShader = shader(gpu_smoke_fragment_spirv, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                    SDL_GPU_SHADERFORMAT_SPIRV, "PSMain");
        } else if (actual == "direct3d12") {
            vertexShader = shader(gpu_smoke_vertex_dxil, SDL_GPU_SHADERSTAGE_VERTEX,
                                  SDL_GPU_SHADERFORMAT_DXIL, "VSMain");
            fragmentShader = shader(gpu_smoke_fragment_dxil, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                    SDL_GPU_SHADERFORMAT_DXIL, "PSMain");
        } else {
            throw std::runtime_error("Unexpected SDL GPU driver: " + std::string(actual));
        }
        source = texture(2, 2, COLOR_FORMAT, SDL_GPU_TEXTUREUSAGE_SAMPLER, "create source texture");
        scene = texture(WIDTH, HEIGHT, COLOR_FORMAT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                        SDL_GPU_TEXTUREUSAGE_SAMPLER, "create scene target");
        depth = texture(WIDTH, HEIGHT, DEPTH_FORMAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                        SDL_GPU_TEXTUREUSAGE_SAMPLER, "create sampled depth target");
        finalColor = texture(WIDTH, HEIGHT, COLOR_FORMAT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                             SDL_GPU_TEXTUREUSAGE_SAMPLER, "create final color target");

        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = require(SDL_CreateGPUSampler(device, &samplerInfo), "create nearest sampler");
        const SDL_GPUBufferCreateInfo bufferInfo{SDL_GPU_BUFFERUSAGE_VERTEX, 18 * sizeof(Vertex), 0};
        vertices = require(SDL_CreateGPUBuffer(device, &bufferInfo), "allocate vertices");
        const SDL_GPUTransferBufferCreateInfo uploadInfo{
            SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, TEXTURE_OFFSET + 2 * TEXTURE_ROW_BYTES, 0
        };
        upload = require(SDL_CreateGPUTransferBuffer(device, &uploadInfo), "allocate upload buffer");
        const SDL_GPUTransferBufferCreateInfo readInfo{
            SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, WIDTH * HEIGHT * 4, 0
        };
        download = require(SDL_CreateGPUTransferBuffer(device, &readInfo), "allocate download buffer");
        scenePipeline = pipeline(true);
        compositePipeline = pipeline(false);
    }

    void fillUploadBuffer() {
        const auto nearer = quad(0.25f, 0.25f, 0.25f, 0.0f, 1.0f);
        const auto farther = quad(0.75f, 0.75f, 0.75f, 0.25f, 0.25f);
        const auto fullscreen = quad(0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
        constexpr Uint8 top[]{255, 0, 0, 255, 0, 0, 255, 255};
        constexpr Uint8 bottom[]{0, 255, 0, 255, 0, 0, 255, 255};
        auto* data = static_cast<Uint8*>(require(SDL_MapGPUTransferBuffer(device, upload, false), "map upload"));
        std::memcpy(data, nearer.data(), sizeof(nearer));
        std::memcpy(data + sizeof(nearer), farther.data(), sizeof(farther));
        std::memcpy(data + sizeof(nearer) + sizeof(farther), fullscreen.data(), sizeof(fullscreen));
        std::memcpy(data + TEXTURE_OFFSET, top, sizeof(top));
        std::memcpy(data + TEXTURE_OFFSET + TEXTURE_ROW_BYTES, bottom, sizeof(bottom));
        SDL_UnmapGPUTransferBuffer(device, upload);
    }

    void recordUpload(SDL_GPUCommandBuffer* command) {
        auto* pass = SDL_BeginGPUCopyPass(command);
        const SDL_GPUTransferBufferLocation from{upload, 0};
        const SDL_GPUBufferRegion to{vertices, 0, 18 * sizeof(Vertex)};
        SDL_UploadToGPUBuffer(pass, &from, &to, false);
        const SDL_GPUTextureTransferInfo pixels{upload, TEXTURE_OFFSET, TEXTURE_ROW_BYTES / 4, 2};
        const SDL_GPUTextureRegion image{source, 0, 0, 0, 0, 0, 2, 2, 1};
        SDL_UploadToGPUTexture(pass, &pixels, &image, false);
        SDL_EndGPUCopyPass(pass);
    }

    void draw(SDL_GPURenderPass* pass, SDL_GPUTexture* sampled, Uint32 firstVertex) {
        const SDL_GPUTextureSamplerBinding binding{sampled, sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        const SDL_GPUBufferBinding buffer{vertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &buffer, 1);
        SDL_DrawGPUPrimitives(pass, 6, 1, firstVertex, 0);
    }

    void recordScene(SDL_GPUCommandBuffer* command) {
        SDL_GPUColorTargetInfo color{};
        color.texture = scene;
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = depth;
        depthTarget.clear_depth = 1.0f;
        depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(command, &color, 1, &depthTarget);
        SDL_BindGPUGraphicsPipeline(pass, scenePipeline);
        draw(pass, source, 0);
        draw(pass, source, 6);
        SDL_EndGPURenderPass(pass);

        color.texture = finalColor;
        pass = SDL_BeginGPURenderPass(command, &color, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, compositePipeline);
        SDL_GPUViewport viewport{0.0f, 0.0f, WIDTH / 2.0f, HEIGHT, 0.0f, 1.0f};
        SDL_SetGPUViewport(pass, &viewport);
        draw(pass, scene, 12);
        viewport.x = WIDTH / 2.0f;
        SDL_SetGPUViewport(pass, &viewport);
        draw(pass, depth, 12);
        SDL_EndGPURenderPass(pass);
    }

    void save(const std::string& path) {
        int pixelWidth = 0;
        int pixelHeight = 0;
        if (window) {
            require(SDL_SetWindowSize(window, WIDTH + 64, HEIGHT + 32), "resize SDL window");
            require(SDL_SyncWindow(window), "wait for SDL window resize");
            require(SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight), "query SDL window pixel size");
        }
        fillUploadBuffer();
        std::string frameError;
        auto frame = host.beginFrame(&frameError);
        if (!frame) {
            throw std::runtime_error(frameError);
        }
        auto* command = frame.command;
        auto* swapchain = frame.swapchain;
        if (window && (!swapchain || frame.width != static_cast<Uint32>(pixelWidth) ||
                       frame.height != static_cast<Uint32>(pixelHeight))) {
            throw std::runtime_error("SDL swapchain does not match the resized window's pixel size");
        }
        recordUpload(command);
        recordScene(command);
        auto* pass = SDL_BeginGPUCopyPass(command);
        const SDL_GPUTextureRegion image{finalColor, 0, 0, 0, 0, 0, WIDTH, HEIGHT, 1};
        const SDL_GPUTextureTransferInfo pixels{download, 0, WIDTH, HEIGHT};
        SDL_DownloadFromGPUTexture(pass, &image, &pixels);
        SDL_EndGPUCopyPass(pass);
        if (swapchain) {
            SDL_GPUBlitInfo blit{};
            blit.source = {finalColor, 0, 0, 0, 0, WIDTH, HEIGHT};
            blit.destination = {swapchain, 0, 0, 0, 0, frame.width, frame.height};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            SDL_BlitGPUTexture(command, &blit);
        }
        if (!host.submit(frame, &fence, &frameError)) {
            throw std::runtime_error(frameError);
        }
        require(SDL_WaitForGPUFences(device, true, &fence, 1), "wait for download");
        std::vector<std::uint8_t> rows(WIDTH * HEIGHT * 4);
        const auto* data = static_cast<const Uint8*>(
            require(SDL_MapGPUTransferBuffer(device, download, false), "map download"));
        for (Uint32 y = 0; y < HEIGHT; ++y) {
            std::memcpy(rows.data() + (HEIGHT - y - 1) * WIDTH * 4, data + y * WIDTH * 4, WIDTH * 4);
        }
        SDL_UnmapGPUTransferBuffer(device, download);
        std::string error;
        if (!PngWriter::writeRgba8(path, WIDTH, HEIGHT, rows, &error)) {
            throw std::runtime_error("write PNG: " + error);
        }
        const auto pixel = [&](Uint32 x, Uint32 y) {
            return rows.data() + ((HEIGHT - y - 1) * WIDTH + x) * 4;
        };
        const auto* top = pixel(WIDTH / 4, HEIGHT / 4);
        const auto* bottom = pixel(WIDTH / 4, 3 * HEIGHT / 4);
        const auto* depthPixel = pixel(3 * WIDTH / 4, HEIGHT / 2);
        if (top[0] != 255 || top[1] != 0 || top[2] != 0 ||
            bottom[0] != 0 || bottom[1] != 255 || bottom[2] != 0 ||
            depthPixel[0] < 63 || depthPixel[0] > 65) {
            throw std::runtime_error("PNG pixels differ from the expected red top, green bottom and 0.25 depth");
        }
    }
};

void checkWindowInput(SDL_Window* window) {
    SDLInputBackend input(window);
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        SDLInputBackend::processEvent(event);
    }
    input.consumeScrollDelta();
    event = {};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = SDL_GetWindowID(window);
    event.wheel.y = 1.5f;
    if (!SDLInputBackend::processEvent(event) || input.consumeScrollDelta() != 1.5 ||
        input.consumeScrollDelta() != 0.0) {
        throw std::runtime_error("SDL input failed to consume the window's wheel event");
    }
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> child(
        SDL_CreateWindow("Secondary SDL input window", WIDTH, HEIGHT, SDL_WINDOW_HIDDEN),
        &SDL_DestroyWindow);
    if (!child) {
        throw std::runtime_error(std::string("create secondary input window: ") + SDL_GetError());
    }
    if (!SDLInputBackend::processEvent(event) || !input.setWindow(child.get()) ||
        input.consumeScrollDelta() != 0.0 || SDLInputBackend::processEvent(event)) {
        throw std::runtime_error("SDL input did not drop the previous window's scroll on rebind");
    }
    event.wheel.windowID = SDL_GetWindowID(child.get());
    if (!SDLInputBackend::processEvent(event) || input.consumeScrollDelta() != 1.5) {
        throw std::runtime_error("SDL input did not route the secondary window's scroll");
    }
    child.reset();
    if (!input.setWindow(window) || SDLInputBackend::processEvent(event)) {
        throw std::runtime_error("SDL input did not release the destroyed window's registration");
    }
    std::cout << "SDL input reassignment and destroyed-window cleanup verified\n";
}
}

int main(int argc, char** argv) {
    std::string driver = "auto";
    std::string path = "recubin-gpu-smoke.png";
    bool offscreen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.starts_with("--gpu-driver=")) {
            driver = arg.substr(13);
        } else if (arg.starts_with("--png=")) {
            path = arg.substr(6);
        } else if (arg == "--offscreen") {
            offscreen = true;
        } else {
            std::cerr << "Usage: RecubinGpuSmoke [--gpu-driver=auto|vulkan|direct3d12]"
                         " [--png=path] [--offscreen]\n";
            return 2;
        }
    }
    if (driver != "auto" && driver != "vulkan" && driver != "direct3d12") {
        std::cerr << "Unknown GPU driver: " << driver << '\n';
        return 2;
    }
    try {
        Smoke smoke;
        smoke.initialize(driver, offscreen);
        if (smoke.window) {
            checkWindowInput(smoke.window);
        }
        smoke.save(path);
        std::cout << "Depth test, depth sampling and PNG verified: " << path << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
