#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <include/GL/glew.h>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/Workspace.hpp>
#include <Instances/Instance.hpp>
#include <Math/Vector3.hpp>
#include <Math/Quaternion.hpp>
#include <Editor/CommandHistory.hpp>
#include <memory>
#include <string>
#include <vector>

class User;

// ===================================================
//  SecondaryViewportPanel  — フローティングビューポート
//  指定Workspaceを独立したカメラで表示するフローティングウィンドウ
// ===================================================
class SecondaryViewportPanel {
public:
    bool m_open = true;
    std::weak_ptr<Workspace> m_workspace;
    std::string m_title;

    // FBO
    GLuint m_fbo      = 0;
    GLuint m_colorTex = 0;
    GLuint m_depthRbo = 0;
    int    m_fbWidth  = 640;
    int    m_fbHeight = 360;

    // フリーカメラ
    Vector3 m_camPos = { 0.0f, 5.0f, 10.0f };
    float   m_yaw    = 180.0f;
    float   m_pitch  = -10.0f;
    float   m_speed  = 10.0f;

    // ---- 編集機能: メインViewportPanelと共有するポインタ ----
    Instance**              selectedInstance  = nullptr;
    std::vector<Instance*>* selectedInstances = nullptr;
    User*                   user              = nullptr;
    CommandHistory*         m_history         = nullptr;

    // ギズモ操作モード（ViewportPanelの値を参照する）
    ImGuizmo::OPERATION* gizmoOp     = nullptr;
    bool*  selectOnly                = nullptr;
    bool*  snapTranslate             = nullptr;
    float* snapTranslateVal          = nullptr;
    bool*  snapRotate                = nullptr;
    float* snapRotateVal             = nullptr;

    explicit SecondaryViewportPanel(std::weak_ptr<Workspace> ws, const std::string& title);
    ~SecondaryViewportPanel();

    void onRender();

private:
    void initFBO(int w, int h);
    void resizeFBO(int w, int h);
    void destroyFBO();
    Quaternion getCamRot() const;

    // ギズモ undo 用状態
    bool m_wasUsingGizmo = false;
    std::vector<MultiGizmoCommand::Entry> m_gizmoEntries;
};
