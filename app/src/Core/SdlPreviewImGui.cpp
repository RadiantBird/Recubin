#include "Core/SdlPreviewImGui.hpp"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

SdlPreviewImGui::~SdlPreviewImGui()
{
    cleanup();
}

bool SdlPreviewImGui::initialize(SDL_Window& window,
                                 SDL_GPUDevice& device,
                                 SDL_GPUTextureFormat colorTargetFormat,
                                 std::string& error)
{
    cleanup();
    error.clear();

    m_window = &window;
    m_device = &device;
    m_previousContext = ImGui::GetCurrentContext();
    m_context = ImGui::CreateContext();
    if (m_context == nullptr)
    {
        error = "ImGui::CreateContext failed";
        cleanup();
        return false;
    }

    ImGui::SetCurrentContext(m_context);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    if (!ImGui_ImplSDL3_InitForSDLGPU(m_window))
    {
        error = "ImGui SDL3 platform initialization failed";
        cleanup();
        return false;
    }
    m_platformInitialized = true;

    ImGui_ImplSDLGPU3_InitInfo initInfo{};
    initInfo.Device = m_device;
    initInfo.ColorTargetFormat = colorTargetFormat;
    if (!ImGui_ImplSDLGPU3_Init(&initInfo))
    {
        error = "ImGui SDL GPU renderer initialization failed";
        cleanup();
        return false;
    }
    m_rendererInitialized = true;

    m_initialized = true;
    ImGui::SetCurrentContext(m_previousContext);
    return true;
}

void SdlPreviewImGui::processEvent(const SDL_Event& event)
{
    if (!m_initialized || m_context == nullptr)
    {
        return;
    }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui::SetCurrentContext(previousContext);
}

bool SdlPreviewImGui::draw(SDL_GPUCommandBuffer& commandBuffer,
                           SDL_GPUTexture& destination,
                           SDL_GPUTexture& scene,
                           std::string& error)
{
    error.clear();
    if (!m_initialized || m_context == nullptr)
    {
        error = "ImGui SDL GPU renderer is not initialized";
        return false;
    }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    m_wantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    ImGui::SetNextWindowSize(ImVec2(960.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene"))
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float sceneAspect = displaySize.x > 0.0f && displaySize.y > 0.0f
                                      ? displaySize.x / displaySize.y
                                      : 1.0f;
        ImVec2 imageSize = available;
        if (imageSize.x > 0.0f && imageSize.y > 0.0f)
        {
            if (imageSize.x / imageSize.y > sceneAspect)
            {
                imageSize.x = imageSize.y * sceneAspect;
            }
            else
            {
                imageSize.y = imageSize.x / sceneAspect;
            }
        }

        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + (available.x - imageSize.x) * 0.5f,
                                   cursor.y + (available.y - imageSize.y) * 0.5f));
        const ImTextureID textureId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(&scene));
        ImGui::Image(ImTextureRef(textureId), imageSize);
    }
    ImGui::End();

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    const auto sceneId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(&scene));
    m_hasSceneImage = false;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.ElemCount != 0 && command.TexRef._TexData == nullptr &&
                command.TexRef.GetTexID() == sceneId) {
                m_hasSceneImage = true;
            }
        }
    }
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, &commandBuffer);

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
        ImGui::SetCurrentContext(previousContext);
        return false;
    }

    ImGui_ImplSDLGPU3_RenderDrawData(drawData, &commandBuffer, renderPass);
    SDL_EndGPURenderPass(renderPass);
    ImGui::SetCurrentContext(previousContext);
    return true;
}

void SdlPreviewImGui::cleanup()
{
    if (m_context == nullptr)
    {
        m_window = nullptr;
        m_device = nullptr;
        return;
    }

    ImGuiContext* currentContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
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
    const bool currentContextWasDestroyed = currentContext == m_context;
    ImGui::DestroyContext(m_context);
    m_context = nullptr;
    m_initialized = false;
    ImGui::SetCurrentContext(currentContextWasDestroyed ? m_previousContext : currentContext);
    m_previousContext = nullptr;
    m_window = nullptr;
    m_device = nullptr;
}
