#pragma once
#include <Editor/EditorPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/ConsolePanel.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <Editor/ContentBrowserPanel.hpp>
#include <Editor/ViewportPanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <include/GLFW/glfw3.h>
#include <memory>
#include <string>
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
    std::unique_ptr<ConsolePanel>        consolePanel;
    std::unique_ptr<SceneHierarchyPanel> hierarchyPanel;
    std::unique_ptr<PropertiesPanel>     propertiesPanel;
    std::unique_ptr<ContentBrowserPanel> contentBrowserPanel;
    std::unique_ptr<ViewportPanel>       viewportPanel;

    std::string scenePath      = "assets/scenes/test_scene.yaml";
    std::string pendingLoadPath;  // 非空のとき main.cpp がリロードを実行する

    EditorManager(Workspace* workspace, User* user);

    // DockSpace + 全パネルを描画する（ImGui フレーム内で呼ぶ）
    void render(GLFWwindow* window);
    // 旧互換オーバーロード（window なし）
    void render() { render(nullptr); }

    // Stop 後の workspace リロード時に全パネルのポインタを一括更新する
    void setWorkspace(Workspace* ws);

    // ViewportPanel の FBO へ 3D シーン描画を開始する前に呼ぶ
    void beginViewportRender();

    // 3D シーン描画後に呼ぶ（FBO を解放し Image として表示する）
    void endViewportRender();

    // 現在のモード取得
    bool isEditMode()  const { return mode == EditorMode::Edit;  }
    bool isPlayMode()  const { return mode == EditorMode::Play;  }
    bool isPauseMode() const { return mode == EditorMode::Pause; }

    bool isDirty()  const { return m_isDirty; }
    void markDirty()      { m_isDirty = true; }

    // 未保存確認ダイアログを ImGui モーダルで表示する（毎フレーム render() 内で処理する）
    void requestSaveDialog(GLFWwindow* window);

    CommandHistory m_history;

private:
    Workspace* m_workspace = nullptr;
    User*      m_user      = nullptr;
    bool       m_isDirty   = false;
    std::shared_ptr<Instance> m_clipboard;

    // 未保存ダイアログ関連
    bool        m_showSaveDialog = false;
    GLFWwindow* m_dialogWindow   = nullptr;

    void renderToolbar();
    void applyTheme();
    void handleEditorShortcuts();
    void renderSaveDialog();
    void saveCurrentScene();
};
