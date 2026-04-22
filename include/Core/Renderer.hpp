#pragma once
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <include/Math/Matrix4.hpp>

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

#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>

// 前方宣言（循環インクルード回避）
class EditorManager;

class Renderer {
    public:
        static Renderer* instance;
        unsigned int VBO;
        unsigned int VAO;
        unsigned int EBO;

        unsigned int shaderProgram;

        std::vector<unsigned int> indices = {};

        std::map<std::string, unsigned int> textureCache;

        unsigned int whiteTexture;
        void createWhiteTexture();

        std::string loadShaderSource(const char* filePath);

        // Editor 管理
        std::unique_ptr<EditorManager> editor;

        void init(GLFWwindow* window);
        virtual ~Renderer();

        // 3D シーンのみ描画（FBO へ書き込む場合も同じ関数を使う）
        void renderScene(User &user, Workspace &workspace);

        // メインループから呼ぶ統合描画
        void render(User &user, GLFWwindow* window, Workspace &workspace);

        void renderImGui(User &user, GLFWwindow* window, Workspace &workspace);

        unsigned int loadTexture(const char* path);
};