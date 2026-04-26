#pragma once
#include <Editor/EditorPanel.hpp>
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

    Instance** selectedInstance = nullptr;  // SceneHierarchyPanel と共有
    User*      user             = nullptr;
    Workspace* workspace        = nullptr;

    // フォーカス状態管理フラグ
    bool isViewportFocused = false;    // このViewportが現在フォーカスされているか
    bool isHoveringViewport = false;   // マウスがViewport上にあるか

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
