#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <include/GL/glew.h>
#include <include/imgui/imgui.h>
#include <Instances/Workspace.hpp>
#include <Math/Vector3.hpp>
#include <Math/Quaternion.hpp>
#include <memory>
#include <string>

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

    explicit SecondaryViewportPanel(std::weak_ptr<Workspace> ws, const std::string& title);
    ~SecondaryViewportPanel();

    void onRender();

private:
    void initFBO(int w, int h);
    void resizeFBO(int w, int h);
    void destroyFBO();
    Quaternion getCamRot() const;
};
