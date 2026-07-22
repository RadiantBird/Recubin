#pragma once
#include <Editor/EditorPanel.hpp>
struct PickerState;        // PropertiesPanel.hpp で定義
struct TerrainBrushState;  // PropertiesPanel.hpp で定義
struct DecalPlaceState;    // PropertiesPanel.hpp で定義
class CommandHistory;
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/Instance.hpp>
#include <Instances/Workspace.hpp>
#include <Math/Matrix4.hpp>
#include <Math/Quaternion.hpp>
#include <Core/User.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
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
    ImGuizmo::MODE gizmoMode = ImGuizmo::WORLD;  // Ctrl+L でワールド/ローカルをトグル
    bool selectOnly = false;  // true のとき ImGuizmo を描画しない
    bool toolNone   = false;  // true のとき無操作（選択変更もドラッグもしない、カメラ操作のみ）

    Instance** selectedInstance  = nullptr;  // SceneHierarchyPanel と共有（Primary）
    std::vector<Instance*>* selectedInstances = nullptr;  // 複数選択セット
    User*      user             = nullptr;
    Workspace* workspace        = nullptr;

    // フォーカス状態管理フラグ
    bool isViewportFocused   = false;  // このViewportが現在フォーカスされているか
    bool isHoveringViewport  = false;  // マウスがViewport上にあるか
    bool showPhysicsDebug    = false;  // 物理制約デバッグビジュアライザー（Viewメニューで切替）
    bool m_isDraggingSelected = false; // 選択キューブ上でドラッグ開始したか

    // ボックス選択ステート
    bool   m_isBoxSelecting   = false;
    bool   m_isBoxSelectArmed = false;
    ImVec2 m_boxSelectStart   = {};

    CommandHistory* m_history = nullptr;
    PickerState*    m_picker  = nullptr;
    TerrainBrushState* m_terrainBrush = nullptr;
    DecalPlaceState*   m_decalPlace   = nullptr;
    double m_lastTerrainBrushTime = -1.0; // ブラシの連続適用を間引くための前回適用時刻
    std::vector<TerrainStreamer::VoxelDiffEntry> m_terrainBrushDiff;   // 現在のストロークで変更されたブロックの差分
    std::shared_ptr<Terrain> m_terrainBrushStrokeTarget;               // ストローク中のTerrainインスタンス（Undo用に保持）

    // ギズモ / フリードラッグ undo 用状態
    bool m_wasUsingGizmo = false;
    bool m_wasDraggingSelected = false; // 前フレームの m_isDraggingSelected（同一フレーム内更新の影響を受けない値）
    std::vector<MultiGizmoCommand::Entry> m_gizmoEntries;
    std::vector<MultiGizmoCommand::Entry> m_freeDragEntries;

    // Roblox スタイルリサイズ: SCALE ドラッグ開始時の状態
    Vector3 m_scaleBeforeSize;
    Vector3 m_scaleBeforeWorldPos;

    // 複数選択 ROTATE: ドラッグ中の回転中心とギズモ回転状態
    Vector3 m_multiRotatePivot;
    Quaternion m_multiRotateGizmoStartRot;
    Quaternion m_multiRotateGizmoCurRot;
    bool m_multiRotatePivotActive = false;

    // Tab キー長押し中のみ有効な移動ピボット（ギズモをマウス位置へ持ってくる。離すと解除）
    bool m_pivotActive = false;
    Vector3 m_pivotWorld;
    Instance* m_pivotOwner = nullptr;
    int m_pivotOp = 0;

    // スナップ・衝突フィット設定（ツールバーから操作）
    bool  snapTranslate    = false;
    float snapTranslateVal = 1.0f;
    bool  snapRotate       = false;
    float snapRotateVal    = 15.0f;
    bool  snapScale        = false;
    float snapScaleVal     = 1.0f;
    bool  collisionFit     = true;

    // 独立カメラ（セカンダリビューポート用。プライマリは user カメラを使う）
    bool    m_useOwnCamera = false;
    Vector3 m_camPos;
    float   m_camYaw   = 0.0f;   // 度
    float   m_camPitch = 0.0f;   // 度
    bool    m_ownCamDragging = false;  // 右ドラッグでカーソルロック中か

    void initOwnCameraFrom(const User& u);  // userカメラの位置/向きから初期化
    Quaternion ownCamRot() const;           // yaw/pitchから回転を合成
    // カメラアクセサ（own/userカメラを切替）
    Vector3 camPos() const;
    Vector3 camForward() const;
    Vector3 camRight() const;
    Vector3 camUp() const;

    // ---- ツールモード状態クエリ ----
    bool isNoToolMode()      const { return toolNone; }
    bool isSelectMode()      const { return !toolNone && selectOnly; }
    bool isMoveMode()        const { return !toolNone && !selectOnly && gizmoOp == ImGuizmo::TRANSLATE; }
    bool isResizeMode()      const { return !toolNone && !selectOnly && gizmoOp == ImGuizmo::SCALE; }
    bool isRotateMode()      const { return !toolNone && !selectOnly && gizmoOp == ImGuizmo::ROTATE; }
    bool isGizmoMode()       const { return isMoveMode() || isResizeMode() || isRotateMode(); }
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
