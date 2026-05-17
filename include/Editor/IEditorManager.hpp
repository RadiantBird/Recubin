#pragma once
struct GLFWwindow;
class Instance;
class User;
class Workspace;

class IEditorManager {
public:
    virtual ~IEditorManager() = default;

    virtual void beginViewportRender() = 0;
    virtual void endViewportRender()   = 0;

    virtual void render(GLFWwindow* window) = 0;

    // ビューポートサイズ取得（エディター: FBO サイズ、ランタイム: ウィンドウサイズ）
    virtual void getViewportSize(GLFWwindow* window, int& w, int& h) = 0;

    // 選択中インスタンス（ランタイム: nullptr を返す）
    virtual Instance* getSelectedInstance() = 0;

    // ImGui 描画前のフレームバッファクリア（ランタイム: no-op）
    virtual void clearForImGui(GLFWwindow* window) = 0;

    // ImGui フレーム描画（ランタイム: no-op）
    virtual void renderUI(User& user, GLFWwindow* window, Workspace& workspace) = 0;
};
