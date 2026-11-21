#pragma once

#include <include/imgui/imgui.h>

// 確認ダイアログ用の色分けボタン。
// dangerButton: 赤。popupOpenedAt(ImGui::GetTime()基準)からcooldownSec経過するまで
//               無効化+残り秒数を表示し、衝動的なクリックを防ぐ。
// safeButton  : 緑。即座に押せる安全側の操作用。
namespace EditorUi {
    // glassButtonの光沢・縁の配色。テーマごとに差し替える。
    // Classicの値は従来のハードコード値と同一（見た目を変えないこと）。
    struct GlassPalette {
        ImVec4 selectedHighlight;  // 選択時に状態色へ混ぜる色
        float  selectedMixScale;   // 選択時の混合量の倍率
        ImVec4 topTint;            // 上端グラデーションへ混ぜる色
        float  bottomScale;        // 下端の明度倍率（Classicは1.0）
        ImVec4 sheenTint;          // 上面の光沢色(RGB。wは光沢の基準アルファ倍率に使わない)
        float  sheenAlphaScale;    // 光沢の強さ倍率
        ImVec4 highlightActive;    // 上端の細いハイライト線(押下)
        ImVec4 highlightHot;       // 同(選択/ホバー)
        ImVec4 highlightRest;      // 同(通常)
        float  borderScale;        // 縁の暗さ（状態色への乗数）
        bool   lightSurface;       // 明るい面のテーマか（他パネルの配色判断用）
    };

    GlassPalette classicGlassPalette();
    GlassPalette frutigerGlassPalette();
    void setGlassPalette(const GlassPalette& palette);
    const GlassPalette& glassPalette();

    // Existing ImGui button behavior with a layered blue-glass background.
    // selected is kept separate from ImGui's transient hovered/active state so
    // toolbar callers can retain a visible tool selection.
    bool glassButton(const char* label, const ImVec2& size = ImVec2(0, 0), bool selected = false);

    const float dangerCooldownSec = 1.0f;
    bool dangerButton(const char* label, double popupOpenedAt, float cooldownSec = dangerCooldownSec);
    bool safeButton(const char* label);
}
