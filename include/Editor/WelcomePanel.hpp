#pragma once
#include <Editor/EditorPanel.hpp>
#include <functional>
#include <string>
#include <include/imgui/imgui.h>

// ===================================================
//  WelcomePanel  — 起動時に表示する「ようこそ」タブ
// ===================================================
class WelcomePanel : public EditorPanel {
public:
    std::string lastScenePath;   // 前回開いていたシーン（main.cppが起動時に設定。空なら「前回の続きから」を無効化）
    ImGuiID dockspaceId = 0;     // 中央ドックへ初回ドッキングするためのID（EditorManagerが毎フレーム設定）
    std::function<void()> onNewScene;
    std::function<void()> onLoadLast;
    std::function<bool()> onOpenScene; // ダイアログでパスが選ばれたら true

    WelcomePanel();
    void onRender() override;
};
