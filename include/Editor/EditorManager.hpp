#pragma once
#include <Editor/IEditorManager.hpp>
#include <Editor/EditorPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/ConsolePanel.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <Editor/ContentBrowserPanel.hpp>
#include <Editor/ViewportPanel.hpp>
#include <Editor/SecondaryViewportPanel.hpp>
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
class EditorManager : public IEditorManager {
public:
    EditorMode mode = EditorMode::Edit;

    // 各パネル（所有権あり）
    std::unique_ptr<ConsolePanel>        consolePanel;
    std::unique_ptr<SceneHierarchyPanel> hierarchyPanel;
    std::unique_ptr<PropertiesPanel>     propertiesPanel;
    std::unique_ptr<ContentBrowserPanel> contentBrowserPanel;
    std::unique_ptr<ViewportPanel>       viewportPanel;

    // セカンダリビューポート（複数可）
    std::vector<std::unique_ptr<SecondaryViewportPanel>> secondaryViewports;
    void openSecondaryViewport(Workspace* ws);

    std::string scenePath      = "assets/scenes/test_scene.yaml";
    std::string pendingLoadPath;  // 非空のとき main.cpp がリロードを実行する

    EditorManager(Workspace* workspace, User* user, Instance* system = nullptr);

    // DockSpace + 全パネルを描画する（ImGui フレーム内で呼ぶ）
    void render(GLFWwindow* window) override;
    // 旧互換オーバーロード（window なし）
    void render() { render(nullptr); }

    // Stop 後の workspace リロード時に全パネルのポインタを一括更新する
    void setWorkspace(Workspace* ws);

    // ViewportPanel の FBO へ 3D シーン描画を開始する前に呼ぶ
    void beginViewportRender() override;

    // 3D シーン描画後に呼ぶ（FBO を解放し Image として表示する）
    void endViewportRender() override;

    // IEditorManager 追加メソッド
    void getViewportSize(GLFWwindow* window, int& w, int& h) override;
    unsigned int getViewportFBO() override;
    bool isViewportFocused() override;
    Instance* getSelectedInstance() override;
    void clearForImGui(GLFWwindow* window) override;
    void renderUI(User& user, GLFWwindow* window, Workspace& workspace) override;

    // 現在のモード取得
    bool isEditMode()  const { return mode == EditorMode::Edit;  }
    bool isPlayMode()  const { return mode == EditorMode::Play;  }
    bool isPauseMode() const { return mode == EditorMode::Pause; }

    bool isDirty()  const { return m_isDirty; }
    void markDirty()      { m_isDirty = true; }

    // 未保存確認ダイアログを ImGui モーダルで表示する（毎フレーム render() 内で処理する）
    void requestSaveDialog(GLFWwindow* window);

    CommandHistory m_history;
    PickerState    m_picker;   // PickerState は PropertiesPanel.hpp で定義

    template <typename T, typename... Args>
    void tryAddObject(const std::string& menuLabel, const std::string& defaultName, Args&&... args);

private:
    Workspace* m_workspace = nullptr;
    Instance*  m_system    = nullptr;
    User*      m_user      = nullptr;
    bool       m_isDirty   = false;
    std::shared_ptr<Instance> m_clipboard;

public:
    void clearClipboard() { m_clipboard.reset(); }

private:

    // 未保存ダイアログ関連
    bool        m_showSaveDialog = false;
    GLFWwindow* m_dialogWindow   = nullptr;

    // パッケージダイアログ関連
    bool        m_showPackageDialog = false;
    char        m_pkgName[256]      = {};
    char        m_pkgOutDir[512]    = {};
    bool        m_isPackaging       = false;
    std::vector<std::string> m_pkgLog;

public:
    std::string engineExePath;

private:
    void renderToolbar();
    void applyTheme();
    void handleEditorShortcuts();
    void renderSaveDialog();
    void renderPackageDialog();
    void saveCurrentScene();
    void openSceneDialog();
    void cleanupOrphanedSelection();
};
