#include "Core/SdlImGuiBackend.hpp"

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "imgui.h"

namespace
{
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
    m_initialized = true;
    return true;
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
                             std::string& error)
{
    error.clear();
    if (!m_initialized || m_context == nullptr)
    {
        error = "ImGui SDL GPU renderer is not initialized";
        return false;
    }

    ImGuiContextGuard contextGuard(m_context);
    ImGui_ImplSDLGPU3_PrepareDrawData(&drawData, &commandBuffer);

    SDL_GPUColorTargetInfo targetInfo{};
    targetInfo.texture = &destination;
    targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
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

    ImGui_ImplSDLGPU3_RenderDrawData(&drawData, &commandBuffer, renderPass);
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
