#pragma once
#include <Editor/EditorPanel.hpp>
#include <Editor/ConsolePanel.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <Editor/ContentBrowserPanel.hpp>
#include <Editor/ViewportPanel.hpp>
#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#include <memory>
#include <vector>

// ===================================================
//  エディターの実行モード
// ===================================================
enum class EditorMode {
    Edit,   // 物理/スクリプト 停止
    Play,   // 実行中
    Pause   // ポーズ中
};

// ===================================================
//  EditorManager  — 全パネルを所有・管理するクラス
//  Renderer が保持して renderImGui() から駆動する
// ===================================================
class EditorManager {
public:
    EditorMode mode = EditorMode::Edit;

    // 各パネル（所有権あり）
    std::unique_ptr<ConsolePanel>       consolePanel;
    std::unique_ptr<SceneHierarchyPanel> hierarchyPanel;
    std::unique_ptr<PropertiesPanel>    propertiesPanel;
    std::unique_ptr<ContentBrowserPanel> contentBrowserPanel;
    std::unique_ptr<ViewportPanel>      viewportPanel;

    EditorManager(Workspace* workspace, User* user);

    // DockSpace + 全パネルを描画する（ImGui フレーム内で呼ぶ）
    void render();

    // ViewportPanel の FBO へ 3D シーン描画を開始する前に呼ぶ
    void beginViewportRender();

    // 3D シーン描画後に呼ぶ（FBO を解放し Image として表示する）
    void endViewportRender();

    // 現在のモード取得
    bool isEditMode()  const { return mode == EditorMode::Edit;  }
    bool isPlayMode()  const { return mode == EditorMode::Play;  }
    bool isPauseMode() const { return mode == EditorMode::Pause; }

private:
    void renderToolbar();
    void applyTheme();
};
