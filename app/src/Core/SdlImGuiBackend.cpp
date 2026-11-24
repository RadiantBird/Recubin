#include "Core/SdlImGuiBackend.hpp"
#include <Core/SdlScenePreview.hpp>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "imgui.h"

#include <cstdint>

#include <memory>

namespace
{
struct PlatformWindowData {
    SdlImGuiBackend* backend = nullptr;
    SDL_WindowID windowId = 0;
    SDL_GPUTexture* target = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
};

struct PlatformCapture {
    std::string_view title;
    const std::filesystem::path* path = nullptr;
    bool handled = false;
};

SDL_WindowID platformWindowId(const ImGuiViewport& viewport)
{
    return static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport.PlatformHandle));
}

class ImGuiContextGuard final
{
public:
    explicit ImGuiContextGuard(ImGuiContext* context)
        : m_previous(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(context);
    }

    ~ImGuiContextGuard()
    {
        ImGui::SetCurrentContext(m_previous);
    }

private:
    ImGuiContext* m_previous = nullptr;
};
}

void SdlImGuiBackend::createPlatformWindow(ImGuiViewport* viewport)
{
    auto* backend = static_cast<SdlImGuiBackend*>(ImGui::GetIO().UserData);
    auto data = std::make_unique<PlatformWindowData>();
    data->backend = backend;
    const SDL_WindowID id = platformWindowId(*viewport);
    SDL_Window* window = SDL_GetWindowFromID(id);
    if (!window || !SDL_ClaimWindowForGPUDevice(backend->m_device, window)) {
        backend->m_platformError = "Claim ImGui window " + std::to_string(id) + ": " + SDL_GetError();
        return;
    }
    data->windowId = id;
    viewport->RendererUserData = data.release();
}

void SdlImGuiBackend::destroyPlatformWindow(ImGuiViewport* viewport)
{
    std::unique_ptr<PlatformWindowData> data(static_cast<PlatformWindowData*>(viewport->RendererUserData));
    viewport->RendererUserData = nullptr;
    if (!data) {
        return;
    }
    auto* device = data->backend->m_device;
    if (data->target) {
        SDL_ReleaseGPUTexture(device, data->target);
    }
    if (auto* window = SDL_GetWindowFromID(data->windowId)) {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
}

void SdlImGuiBackend::renderPlatformWindow(ImGuiViewport* viewport, void* renderArg)
{
    auto* data = static_cast<PlatformWindowData*>(viewport->RendererUserData);
    if (!data || !viewport->DrawData) {
        return;
    }
    auto& backend = *data->backend;
    auto fail = [&](const std::string& detail) {
        if (backend.m_platformError.empty()) {
            backend.m_platformError = "ImGui window " + std::to_string(data->windowId) + ": " + detail;
        }
    };
    SDL_Window* window = SDL_GetWindowFromID(data->windowId);
    if (!window) {
        fail("SDL window is unavailable");
        return;
    }
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(backend.m_device);
    if (!command) {
        fail(SDL_GetError());
        return;
    }
    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &swapchain, &width, &height)) {
        fail(SDL_GetError());
    }
    if (!swapchain) {
        if (!SDL_CancelGPUCommandBuffer(command)) {
            fail(SDL_GetError());
        }
        return;
    }
    if (data->target == nullptr || data->width != width || data->height != height) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = backend.m_colorTargetFormat;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        auto* target = SDL_CreateGPUTexture(backend.m_device, &info);
        if (!target) {
            fail(SDL_GetError());
            if (!SDL_SubmitGPUCommandBuffer(command)) {
                fail(SDL_GetError());
            }
            return;
        }
        if (data->target) {
            SDL_ReleaseGPUTexture(backend.m_device, data->target);
        }
        data->target = target;
        data->width = width;
        data->height = height;
    }
    std::string error;
    if (backend.render(*viewport->DrawData, *command, *data->target, error, false)) {
        SDL_GPUBlitInfo blit{};
        blit.source = {data->target, 0, 0, 0, 0, width, height};
        blit.destination = {swapchain, 0, 0, 0, 0, width, height};
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(command, &blit);
        auto* capture = static_cast<PlatformCapture*>(renderArg);
        if (capture && !capture->handled && capture->title == SDL_GetWindowTitle(window)) {
            capture->handled = true;
            if (!captureSdlTargetToPng(*backend.m_device, *command, *data->target,
                                       width, height, *capture->path, error)) {
                fail(error);
            }
            return;
        }
    } else {
        fail(error);
    }
    if (!SDL_SubmitGPUCommandBuffer(command)) {
        fail(SDL_GetError());
    }
}

SdlImGuiBackend::~SdlImGuiBackend()
{
    shutdown();
}

bool SdlImGuiBackend::initialize(ImGuiContext& context,
                                 SDL_Window& window,
                                 SDL_GPUDevice& device,
                                 SDL_GPUTextureFormat colorTargetFormat,
                                 std::string& error)
{
    shutdown();
    error.clear();

    m_window = &window;
    m_device = &device;
    m_context = &context;
    m_colorTargetFormat = colorTargetFormat;
    ImGuiContextGuard contextGuard(m_context);

    if (!ImGui_ImplSDL3_InitForSDLGPU(m_window))
    {
        error = "ImGui SDL3 platform initialization failed";
        shutdown();
        return false;
    }
    m_platformInitialized = true;

    ImGui_ImplSDLGPU3_InitInfo initInfo{};
    initInfo.Device = m_device;
    initInfo.ColorTargetFormat = colorTargetFormat;
    if (!ImGui_ImplSDLGPU3_Init(&initInfo))
    {
        error = "ImGui SDL GPU renderer initialization failed";
        shutdown();
        return false;
    }
    m_rendererInitialized = true;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        if (ImGui::GetIO().UserData) {
            error = "ImGui context already has application user data";
            shutdown();
            return false;
        }
        ImGui::GetIO().UserData = this;
        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        platform.Renderer_CreateWindow = &SdlImGuiBackend::createPlatformWindow;
        platform.Renderer_DestroyWindow = &SdlImGuiBackend::destroyPlatformWindow;
        platform.Renderer_RenderWindow = &SdlImGuiBackend::renderPlatformWindow;
    }
    m_initialized = true;
    return true;
}

bool SdlImGuiBackend::renderPlatformWindows(std::string& error, std::string_view captureTitle,
                                          const std::filesystem::path* capturePath)
{
    error.clear();
    if (!m_initialized || m_context == nullptr)
    {
        error = "ImGui SDL GPU renderer is not initialized";
        return false;
    }
    ImGuiContextGuard contextGuard(m_context);
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        if (capturePath) {
            error = "Detached ImGui windows are unavailable";
        }
        return error.empty();
    }
    m_platformError.clear();
    PlatformCapture capture{captureTitle, capturePath};
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault(nullptr, capturePath ? &capture : nullptr);
    error = m_platformError;
    if (error.empty() && capturePath && !capture.handled) {
        error = "No drawable detached window with title " + std::string(captureTitle);
    }
    return error.empty();
}

bool SdlImGuiBackend::ownsWindow(SDL_Window* window) const
{
    if (!m_initialized || window == nullptr || m_window == nullptr)
    {
        return false;
    }
    if (window == m_window)
    {
        return true;
    }
    ImGuiContextGuard contextGuard(m_context);
    const auto id = SDL_GetWindowID(window);
    return id && ImGui::FindViewportByPlatformHandle(reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
}

void SdlImGuiBackend::processEvent(const SDL_Event& event)
{
    if (!m_initialized || m_context == nullptr)
    {
        return;
    }

    ImGuiContextGuard contextGuard(m_context);
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool SdlImGuiBackend::beginFrame(std::string& error)
{
    error.clear();
    if (!m_initialized || m_context == nullptr)
    {
        error = "ImGui SDL GPU renderer is not initialized";
        return false;
    }

    ImGuiContextGuard contextGuard(m_context);
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLGPU3_NewFrame();
    return true;
}

bool SdlImGuiBackend::render(ImDrawData& drawData,
                             SDL_GPUCommandBuffer& commandBuffer,
                             SDL_GPUTexture& destination,
                             std::string& error,
                             bool loadDestination)
{
    error.clear();
    if (!m_initialized || m_context == nullptr)
    {
        error = "ImGui SDL GPU renderer is not initialized";
        return false;
    }

    ImGuiContextGuard contextGuard(m_context);
    if (loadDestination && drawData.TotalVtxCount == 0)
    {
        return true;
    }
    ImGui_ImplSDLGPU3_PrepareDrawData(&drawData, &commandBuffer);

    SDL_GPUColorTargetInfo targetInfo{};
    targetInfo.texture = &destination;
    targetInfo.load_op = loadDestination ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    targetInfo.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    targetInfo.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(&commandBuffer, &targetInfo, 1, nullptr);
    if (renderPass == nullptr)
    {
        error = SDL_GetError();
        if (error.empty())
        {
            error = "SDL_BeginGPURenderPass failed for ImGui";
        }
        return false;
    }

    if (drawData.TotalVtxCount > 0)
    {
        ImGui_ImplSDLGPU3_RenderDrawData(&drawData, &commandBuffer, renderPass);
    }
    SDL_EndGPURenderPass(renderPass);
    return true;
}

void SdlImGuiBackend::shutdown()
{
    if (m_context != nullptr)
    {
        ImGuiContextGuard contextGuard(m_context);
        if (m_device != nullptr)
        {
            SDL_WaitForGPUIdle(m_device);
        }
        if (m_rendererInitialized)
        {
            ImGui_ImplSDLGPU3_Shutdown();
            m_rendererInitialized = false;
        }
        if (ImGui::GetIO().UserData == this) {
            ImGui::GetIO().UserData = nullptr;
        }
        if (m_platformInitialized)
        {
            ImGui_ImplSDL3_Shutdown();
            m_platformInitialized = false;
        }
    }

    m_initialized = false;
    m_context = nullptr;
    m_window = nullptr;
    m_device = nullptr;
}
