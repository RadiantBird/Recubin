#include "Core/SdlPreviewImGui.hpp"

#include "imgui.h"

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

    if (!m_backend.initialize(*m_context, window, device, colorTargetFormat, error))
    {
        cleanup();
        return false;
    }

    ImGui::SetCurrentContext(m_previousContext);
    return true;
}

void SdlPreviewImGui::processEvent(const SDL_Event& event)
{
    if (!m_backend.isInitialized() || m_context == nullptr)
    {
        return;
    }

    m_backend.processEvent(event);
}

bool SdlPreviewImGui::draw(SDL_GPUCommandBuffer& commandBuffer,
                           SDL_GPUTexture& destination,
                           SDL_GPUTexture& scene,
                           std::string& error)
{
    error.clear();
    if (!m_backend.isInitialized() || m_context == nullptr)
    {
        error = "ImGui SDL GPU renderer is not initialized";
        return false;
    }

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);

    if (!m_backend.beginFrame(error))
    {
        ImGui::SetCurrentContext(previousContext);
        return false;
    }
    ImGui::SetCurrentContext(m_context);
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
    if (drawData == nullptr)
    {
        error = "ImGui draw data is not available";
        ImGui::SetCurrentContext(previousContext);
        return false;
    }
    if (!m_backend.render(*drawData, commandBuffer, destination, error))
    {
        ImGui::SetCurrentContext(previousContext);
        return false;
    }
    ImGui::SetCurrentContext(previousContext);
    return true;
}

void SdlPreviewImGui::cleanup()
{
    if (m_context == nullptr)
    {
        m_previousContext = nullptr;
        return;
    }

    ImGuiContext* currentContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    m_backend.shutdown();
    const bool currentContextWasDestroyed = currentContext == m_context;
    ImGui::DestroyContext(m_context);
    m_context = nullptr;
    ImGui::SetCurrentContext(currentContextWasDestroyed ? m_previousContext : currentContext);
    m_previousContext = nullptr;
}
