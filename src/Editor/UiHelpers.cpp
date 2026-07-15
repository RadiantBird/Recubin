#include <Editor/UiHelpers.hpp>

#include <include/imgui/imgui.h>

#include <cmath>
#include <cstdio>

namespace EditorUi {

bool dangerButton(const char* label, double popupOpenedAt, float cooldownSec) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.12f, 0.12f, 1.0f));

    double elapsed = ImGui::GetTime() - popupOpenedAt;
    bool pressed = false;

    if (elapsed < cooldownSec) {
        int remaining = static_cast<int>(std::ceil(cooldownSec - elapsed));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s (%d)###%s", label, remaining, label);
        ImGui::BeginDisabled();
        ImGui::Button(buf);
        ImGui::EndDisabled();
    } else {
        pressed = ImGui::Button(label);
    }

    ImGui::PopStyleColor(3);
    return pressed;
}

bool safeButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.55f, 0.12f, 1.0f));

    bool pressed = ImGui::Button(label);

    ImGui::PopStyleColor(3);
    return pressed;
}

}
