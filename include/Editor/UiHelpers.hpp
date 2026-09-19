#pragma once

#include <include/imgui/imgui.h>

// 確認ダイアログ用の色分けボタン。
// dangerButton: 赤。popupOpenedAt(ImGui::GetTime()基準)からcooldownSec経過するまで
//               無効化+残り秒数を表示し、衝動的なクリックを防ぐ。
// safeButton  : 緑。即座に押せる安全側の操作用。
namespace EditorUi {
    // Existing ImGui button behavior with a layered blue-glass background.
    // selected is kept separate from ImGui's transient hovered/active state so
    // toolbar callers can retain a visible tool selection.
    bool glassButton(const char* label, const ImVec2& size = ImVec2(0, 0), bool selected = false);

    bool dangerButton(const char* label, double popupOpenedAt, float cooldownSec = 3.0f);
    bool safeButton(const char* label);
}
