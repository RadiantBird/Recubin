#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <include/Math/Matrix4.hpp>
#include <include/Math/Vector2.hpp>
#include <include/Math/Quaternion.hpp>

#include <include/Core/User.hpp>
#include <include/Instances/Cube.hpp>
#include <include/Instances/Workspace.hpp>

#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <memory>
#include <functional>

#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>

// 前方宣言（循環インクルード回避）
class IEditorManager;
class GuiButton;
class SurfaceGui;

struct ViewportRenderDesc {
    GLuint fbo = 0;
    int width = 0;
    int height = 0;
    Vector3 cameraPosition;
    Vector3 cameraForward;
    Vector3 cameraUp;
    Workspace* workspace = nullptr;
    bool renderShadows = true;
    bool renderHighlights = false;
    bool renderConstraints = true;
    bool isFocused = false;
};

class Renderer {
    public:
        static Renderer* instance;
        unsigned int VBO;
        unsigned int VAO;
        unsigned int EBO;

        unsigned int shaderProgram;

        int          lightDirLoc    = -1;
        int          brightnessLoc  = -1;

        unsigned int shadowFBO     = 0;
        unsigned int shadowMapTex  = 0;
        unsigned int depthShader   = 0;
        static const int SHADOW_MAP_SIZE = 2048;

        std::vector<unsigned int> indices = {};

        std::map<std::string, unsigned int> textureCache;

        unsigned int whiteTexture;
        void createWhiteTexture();

        std::string loadShaderSource(const char* filePath);

        // Editor 管理
        std::unique_ptr<IEditorManager> editor;
        GLFWwindow* m_window = nullptr; // init() で保持。renderScene の fallback 解像度に使用

        void init(GLFWwindow* window);
        virtual ~Renderer();

        // 統合されたビューポート描画メソッド
        void renderViewport(const ViewportRenderDesc& desc);

        // メインループから呼ぶ統合描画
        void render(User &user, GLFWwindow* window, Workspace &workspace);

        void renderImGui(User &user, GLFWwindow* window, Workspace &workspace);

        unsigned int loadTexture(const char* path);

        // 制約ビジュアライザ（Rope/Rod）
        GLuint m_lineVAO    = 0;
        GLuint m_lineVBO    = 0;
        GLuint m_lineShader = 0;
        void initLineRenderer();
        void renderConstraints(Workspace& workspace, const Matrix4& view, const Matrix4& projection);

        // GUI 描画
        Matrix4  m_lastView;
        Matrix4  m_lastProj;
        std::function<void(GuiButton*)> m_onButtonActivated;

        void renderScreenGui(Workspace& ws, float vpX, float vpY, float vpW, float vpH);
        void renderWorldGui (Workspace& ws, float vpX, float vpY, float vpW, float vpH);
        void bakeSurfaceGui (SurfaceGui* sg);
};