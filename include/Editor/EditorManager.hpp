#pragma once
#include <Editor/IEditorManager.hpp>
#include <Editor/EditorPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/ConsolePanel.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <Editor/ContentBrowserPanel.hpp>
#include <Editor/ViewportPanel.hpp>
#include <Editor/AnimationEditorPanel.hpp>
#include <Editor/WelcomePanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#include <include/GLFW/glfw3.h>
#include <memory>
#include <string>
#include <vector>
#include <include/imgui/imgui.h>

// ===================================================
//  エディターの実行モード
// ===================================================
enum class EditorMode {
    Edit,   // 物理/スクリプト 停止
    Play,   // 実行中
    Pause   // ポーズ中
};

// ===================================================
//  テストプレイの起動方式
// ===================================================
enum class EditorPlayMode {
    Normal,
    PlayHere,
    LocalServer
};

// ===================================================
//  ツールバーのカテゴリタブ
// ===================================================
enum class ToolbarCategory {
    Basic,
    Cubes,
    Terrain,
    Physics,
    Character
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
    std::unique_ptr<AnimationEditorPanel> animationPanel;
    std::unique_ptr<WelcomePanel>        welcomePanel;

    // セカンダリビューポート（複数可）
    std::vector<std::unique_ptr<ViewportPanel>> secondaryViewports;
    void openSecondaryViewport(Workspace* ws);

    bool isAnyViewportHovered() const;

    // 起動時にmain.cppが決定したシーンパスが入る(前回開いていたシーン、または
    // ユーザーがダイアログで選択したシーン)。construct直後にmain.cppが設定する
    std::string scenePath;
    std::string pendingLoadPath;  // 非空のとき main.cpp がリロードを実行する
    bool pendingNewScene = false; // 新規シーン作成要求（mainループが処理）

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
    bool ownsSceneRender() override;
    void renderUI(User& user, GLFWwindow* window, Workspace& workspace) override;

    // 現在のモード取得
    bool isEditMode()  const { return mode == EditorMode::Edit;  }
    bool isPlayMode()  const { return mode == EditorMode::Play;  }
    bool isPauseMode() const { return mode == EditorMode::Pause; }

    // テストプレイ設定。selected は次回起動用、active は直近の起動要求を保持する。
    EditorPlayMode selectedPlayMode() const;
    void setSelectedPlayMode(EditorPlayMode playMode);
    EditorPlayMode activePlayMode() const;
    int networkClientCount() const;
    void setNetworkClientCount(int count);

    // LocalServer の外部ライフサイクル状態を main.cpp から反映する。
    void setNetworkClientStatus(int connected, int expected);
    void setExternalPlayCleanup(bool cleaningUp);
    bool isExternalPlayCleanup() const;

    // 開始失敗を次の ImGui フレームでローカライズ済みモーダルとして表示する。
    void showPlayStartError(const std::string& detail = {});
    void showLocalServerNetworkRequiredError();

    bool isDirty()  const { return m_isDirty; }
    void markDirty()      { m_isDirty = true; }

    // 未保存確認ダイアログを ImGui モーダルで表示する（毎フレーム render() 内で処理する）
    void requestSaveDialog(GLFWwindow* window);

    // シーンファイルの読み込み要求（Open Scene / Load ボタン共通の入口）
    // Edit モード中は即座に pendingLoadPath へ反映。Play/Pause 中は終了確認ポップアップを挟む
    void requestSceneLoad(const std::string& path);
    void requestNewScene();

    CommandHistory m_history;
    PickerState    m_picker;          // PickerState は PropertiesPanel.hpp で定義
    TerrainBrushState m_terrainBrush; // TerrainBrushState は PropertiesPanel.hpp で定義
    DecalPlaceState   m_decalPlace;   // DecalPlaceState は PropertiesPanel.hpp で定義
    WeldModeState     m_weldMode;

    template <typename T, typename... Args>
    void tryAddObjectButton(const char* icon, const std::string& label, const std::string& defaultName,
                             const std::shared_ptr<Instance>& parent, const ImVec2& btnSize, Args&&... args);

private:
    Workspace* m_workspace = nullptr;
    Instance*  m_system    = nullptr;
    User*      m_user      = nullptr;
    bool       m_isDirty   = false;
    std::vector<std::shared_ptr<Instance>> m_clipboard;  // 複数コピー対応

public:
    void clearClipboard() { m_clipboard.clear(); }

private:

    // 未保存ダイアログ関連
    bool        m_showSaveDialog   = false;
    GLFWwindow* m_dialogWindow     = nullptr;
    double      m_saveDialogOpenedAt = 0.0;

    // テストプレイ中のシーン読み込み確認ダイアログ関連
    bool        m_showPlayLoadConfirm = false;
    std::string m_pendingPlayLoadPath;

    // テストプレイ方式と外部クライアント状態
    EditorPlayMode m_selectedPlayMode = EditorPlayMode::Normal;
    EditorPlayMode m_activePlayMode   = EditorPlayMode::Normal;
    int  m_networkClientCount     = 2;
    int  m_connectedClientCount   = 0;
    int  m_expectedClientCount    = 0;
    bool m_externalPlayCleanup    = false;

    enum class PlayStartErrorKind { Generic, NetworkRequired };
    bool m_showPlayStartError = false;
    PlayStartErrorKind m_playStartErrorKind = PlayStartErrorKind::Generic;
    std::string m_playStartErrorDetail;

    // パッケージダイアログ関連
    bool        m_showPackageDialog = false;
    char        m_pkgName[256]      = {};
    char        m_pkgOutDir[512]    = {};
    bool        m_isPackaging       = false;
    std::vector<std::string> m_pkgLog;
    bool m_pkgLogScrollToBottom = false;

public:
    std::string engineExePath;

private:
    ToolbarCategory m_toolbarCategory = ToolbarCategory::Basic;
    float m_uiLayoutScale = 1.0f;

    // icon(nullptr可)+labelを1つのボタンに描画する。ボタン幅/高さに収まらない場合は
    // ImGui::SetWindowFontScaleで自動的にフォントを縮小する(下限0.55倍)。クリックされたらtrue。
    bool drawIconButton(const char* icon, const char* label, const ImVec2& btnSize);

    void renderToolbar();
    void renderToolbarTabs();
    void renderToolbarBasic();
    void renderToolbarCubes();
    void renderToolbarTerrain();
    void renderToolbarPhysics();
    void renderToolbarCharacter();
    void applyTheme();
    void updateResponsiveScale();
    void handleEditorShortcuts();
    void renderSaveDialog();
    void renderPlayLoadConfirmDialog();
    void renderPlayStartErrorDialog();
    void renderPackageDialog();
    void saveCurrentScene();
    void openSceneDialog();
    void cleanupOrphanedSelection();
};
