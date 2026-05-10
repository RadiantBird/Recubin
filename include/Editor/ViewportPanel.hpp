#pragma once
#include <Editor/EditorPanel.hpp>
struct PickerState;  // PropertiesPanel.hpp で定義
class CommandHistory;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/Instance.hpp>
#include <Instances/Workspace.hpp>
#include <Math/Matrix4.hpp>
#include <Core/User.hpp>
#include <Editor/CommandHistory.hpp>
#include <vector>

// ===================================================
//  ViewportPanel  — FBO 経由でゲームビューを表示
// ===================================================
class ViewportPanel : public EditorPanel {
public:
    GLuint framebuffer       = 0;
    GLuint colorTexture      = 0;
    GLuint depthRenderbuffer = 0;

    int fbWidth  = 1280;
    int fbHeight = 720;

    // ギズモ操作モード
    ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
    bool selectOnly = false;  // true のとき ImGuizmo を描画しない

    Instance** selectedInstance  = nullptr;  // SceneHierarchyPanel と共有（Primary）
    std::vector<Instance*>* selectedInstances = nullptr;  // 複数選択セット
    User*      user             = nullptr;
    Workspace* workspace        = nullptr;

    // フォーカス状態管理フラグ
    bool isViewportFocused   = false;  // このViewportが現在フォーカスされているか
    bool isHoveringViewport  = false;  // マウスがViewport上にあるか
    bool m_isDraggingSelected = false; // 選択キューブ上でドラッグ開始したか

    // ボックス選択ステート
    bool   m_isBoxSelecting   = false;
    bool   m_isBoxSelectArmed = false;
    ImVec2 m_boxSelectStart   = {};

    CommandHistory* m_history = nullptr;
    PickerState*    m_picker  = nullptr;

    // ギズモ / フリードラッグ undo 用状態
    bool m_wasUsingGizmo = false;
    std::vector<MultiGizmoCommand::Entry> m_gizmoEntries;
    std::vector<MultiGizmoCommand::Entry> m_freeDragEntries;

    // スナップ・衝突フィット設定（ツールバーから操作）
    bool  snapTranslate    = false;
    float snapTranslateVal = 1.0f;
    bool  snapRotate       = false;
    float snapRotateVal    = 15.0f;
    bool  collisionFit     = true;

    // ---- ツールモード状態クエリ ----
    bool isSelectMode()      const { return selectOnly; }
    bool isMoveMode()        const { return !selectOnly && gizmoOp == ImGuizmo::TRANSLATE; }
    bool isResizeMode()      const { return !selectOnly && gizmoOp == ImGuizmo::SCALE; }
    bool isRotateMode()      const { return !selectOnly && gizmoOp == ImGuizmo::ROTATE; }
    bool hasMultiSelection() const { return selectedInstances && selectedInstances->size() > 1; }

    ViewportPanel();
    ~ViewportPanel();

    void initFBO(int w, int h);
    void resizeFBO(int w, int h);
    void destroyFBO();

    // FBO にバインドして 3D シーンを描画できる状態にする
    void beginRender();
    // FBO のバインドを解除してパネルに Image() 表示する
    void endRenderAndDisplay();

    void onRender() override;
};
