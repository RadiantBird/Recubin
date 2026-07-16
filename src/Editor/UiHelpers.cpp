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

    // カウントダウン終了後にボタン幅が縮んで誤クリックを誘発しないよう、
    // 常に「ラベル (残秒)」表示時の幅に固定する
    char wide[128];
    std::snprintf(wide, sizeof(wide), "%s (%d)", label, static_cast<int>(std::ceil(cooldownSec)));
    ImVec2 size(ImGui::CalcTextSize(wide).x + ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f);

    if (elapsed < cooldownSec) {
        int remaining = static_cast<int>(std::ceil(cooldownSec - elapsed));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s (%d)###%s", label, remaining, label);
        ImGui::BeginDisabled();
        ImGui::Button(buf, size);
        ImGui::EndDisabled();
    } else {
        pressed = ImGui::Button(label, size);
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
