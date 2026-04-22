#pragma once
#include <string>

// ===================================================
//  EditorPanel  — 全エディターパネルの抽象基底クラス
// ===================================================
class EditorPanel {
public:
    std::string title;
    bool        isOpen = true;

    explicit EditorPanel(std::string title) : title(std::move(title)) {}
    virtual ~EditorPanel() = default;

    // 毎フレーム ImGui::Begin / End を含む描画処理を行う
    virtual void onRender() = 0;
};
