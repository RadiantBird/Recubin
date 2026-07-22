#include <Core/RuntimeChatOverlay.hpp>
#include <Instances/ChatService.hpp>
#include <include/imgui/imgui.h>
#include <algorithm>

void RuntimeChatOverlay::render(ChatService& service, float x, float y, float width, float height) {
    if (!m_open && (ImGui::IsKeyPressed(ImGuiKey_T, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        m_open = true; m_focusInput = true; m_input[0] = '\0'; m_error.clear();
    }
    const float panelWidth = std::max(120.f, std::min(420.f, width - 24.f));
    if (!m_open) {
        ImGui::SetNextWindowPos(ImVec2(x + 12.f, y + 12.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, 132.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.42f);
        ImGui::Begin("##RuntimeChatPreview", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        const auto& messages = service.messages();
        const std::size_t first = messages.size() > 4 ? messages.size() - 4 : 0;
        for (std::size_t i = first; i < messages.size(); ++i)
            ImGui::TextWrapped("[Peer %u] %s", messages[i].senderId, messages[i].text.c_str());
        ImGui::TextDisabled("T / Enter: Chat");
        ImGui::End();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_open = false; return; }
    ImGui::SetNextWindowPos(ImVec2(x + 12.f, y + 12.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, std::max(100.f, std::min(280.f, height - 24.f))), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (ImGui::Begin("##RuntimeChat", &m_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::BeginChild("##ChatHistory", ImVec2(0, -36), false);
        for (const auto& message : service.messages()) ImGui::TextWrapped("[Peer %u] %s", message.senderId, message.text.c_str());
        if (m_seenMessageCount != service.messages().size()) ImGui::SetScrollHereY(1.f);
        m_seenMessageCount = service.messages().size();
        ImGui::EndChild();
        if (m_focusInput) { ImGui::SetKeyboardFocusHere(); m_focusInput = false; }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##ChatInput", m_input, sizeof(m_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (service.sendMessage(m_input)) {
                m_input[0] = '\0';
                m_error.clear();
                m_open = false;
            }
            else m_error = "Message is empty, invalid, too long, or unavailable.";
            m_focusInput = m_open;
        }
        if (!m_error.empty()) ImGui::TextColored(ImVec4(1.f,.35f,.35f,1.f), "%s", m_error.c_str());
    }
    ImGui::End();
}
