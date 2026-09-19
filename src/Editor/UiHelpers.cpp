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

bool glassButton(const char* label, const ImVec2& size, bool selected) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Keep ImGui in charge of layout, clipping, ID handling, and interaction.
    // Its frame is made transparent and the replacement frame is emitted into
    // an earlier draw-list channel, so it cannot cover the button text.
    const ImVec4 buttonColor = style.Colors[ImGuiCol_Button];
    const ImVec4 hoveredColor = style.Colors[ImGuiCol_ButtonHovered];
    const ImVec4 activeColor = style.Colors[ImGuiCol_ButtonActive];
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const auto mix = [](const ImVec4& a, const ImVec4& b, float amount) {
        return ImVec4(
            a.x + (b.x - a.x) * amount,
            a.y + (b.y - a.y) * amount,
            a.z + (b.z - a.z) * amount,
            a.w + (b.w - a.w) * amount);
    };
    const auto scale = [](const ImVec4& color, float amount, float alpha) {
        return ImVec4(color.x * amount, color.y * amount, color.z * amount, alpha);
    };

    ImVec4 stateColor = active ? activeColor : (hovered ? hoveredColor : buttonColor);
    if (selected) {
        // Brighten the current semantic color instead of replacing it with blue:
        // selected Weld/Play controls must retain their green meaning.
        const ImVec4 selectedHighlight(0.62f, 0.82f, 1.0f, 1.0f);
        stateColor = mix(stateColor, selectedHighlight, hovered ? 0.34f : 0.24f);
    }

    const float topLightness = active ? 0.16f : (selected ? 0.36f : (hovered ? 0.32f : 0.27f));
    const ImVec4 topColor = mix(stateColor, ImVec4(0.82f, 0.93f, 1.0f, 1.0f),
                                topLightness);
    const ImVec4 bottomColor = scale(stateColor,
                                     active ? 0.56f : (hovered || selected ? 0.74f : 0.64f), 1.0f);
    const ImVec4 borderColor = scale(stateColor, 0.34f, 0.90f);
    const float rounding = style.FrameRounding;
    const ImVec2 innerMin(min.x + 1.0f, min.y + 1.0f);
    const ImVec2 innerMax(max.x - 1.0f, max.y - 1.0f);

    drawList->ChannelsSetCurrent(0);
    // Clip all custom pixels to the item rectangle, including the sheen and
    // edge accents. The rounded frame/border supplies the existing UI shape.
    drawList->PushClipRect(min, max, true);
    drawList->AddRectFilledMultiColor(
        innerMin, innerMax,
        ImGui::GetColorU32(topColor), ImGui::GetColorU32(topColor),
        ImGui::GetColorU32(bottomColor), ImGui::GetColorU32(bottomColor));

    const float sheenBottom = min.y + (max.y - min.y) * 0.32f;
    const ImVec4 sheenTop(0.78f, 0.93f, 1.0f,
                           active ? 0.14f : (selected || hovered ? 0.38f : 0.28f));
    const ImVec4 sheenFade(0.75f, 0.90f, 1.0f, 0.0f);
    drawList->AddRectFilledMultiColor(
        innerMin, ImVec2(innerMax.x, sheenBottom),
        ImGui::GetColorU32(sheenTop), ImGui::GetColorU32(sheenTop),
        ImGui::GetColorU32(sheenFade), ImGui::GetColorU32(sheenFade));

    const ImU32 border = ImGui::GetColorU32(borderColor);
    drawList->AddRect(min, max, border, rounding, 0, 1.0f);
    const ImVec4 topHighlight = active
        ? ImVec4(0.44f, 0.64f, 0.90f, 0.36f)
        : selected || hovered
        ? ImVec4(0.68f, 0.88f, 1.0f, 0.82f)
        : ImVec4(0.52f, 0.74f, 1.0f, 0.50f);
    drawList->AddLine(ImVec2(min.x + rounding + 1.0f, min.y + 1.0f),
                      ImVec2(max.x - rounding - 1.0f, min.y + 1.0f),
                      ImGui::GetColorU32(topHighlight), 1.0f);
    const ImU32 lowerBorder = ImGui::GetColorU32(scale(stateColor, 0.24f, 0.78f));
    drawList->AddLine(ImVec2(min.x + rounding, max.y - 1.0f),
                      ImVec2(max.x - rounding, max.y - 1.0f), lowerBorder, 1.0f);
    drawList->AddLine(ImVec2(max.x - 1.0f, min.y + rounding),
                      ImVec2(max.x - 1.0f, max.y - rounding), lowerBorder, 1.0f);
    drawList->PopClipRect();
    drawList->ChannelsMerge();
    return pressed;
}

}
