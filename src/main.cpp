#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Math/Matrix4.hpp>

#include <Instances/Cube.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Script.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstddef>

#include <PhysX/PxPhysicsAPI.h>

GLFWwindow* setupWindow() {
    std::cout << "initing GLFW...\n";
    if (!glfwInit()) {
        std::cout << "GLFW init failed\n";
        return nullptr;
    }

    std::cout << "creating window...\n";
    GLFWwindow* window = glfwCreateWindow(800, 600, "Welcome to Recubin", nullptr, nullptr);
    if (!window) {
        std::cout << "Window creation failed\n";
        glfwTerminate();
        return nullptr;
    }

    // アスペクト比を固定 (800 : 600 = 4 : 3)
    glfwSetWindowAspectRatio(window, 800, 600);

    std::cout << "making context...\n";
    glfwMakeContextCurrent(window);

    std::cout << "initing GLEW...\n";
    if (glewInit() != GLEW_OK) {
        std::cout << "GLEW init failed\n";
        return nullptr;
    }
    return window;
}

int main() {
    std::cout << "Hello world!\n"
              << "Version 0.74\n";
    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    Renderer renderer;
    Physics physicsEngine;
    User user(window);
    LuauEngine luauEngine;

    Instance system("System");

    physicsEngine.init();
    renderer.init();

    Workspace* workspace = static_cast<Workspace*>(SceneLoader::loadScene("assets/scenes/test_scene.yaml"));
    if (!workspace) {
        std::cerr << "[ERROR] Failed to load scene. Creating empty workspace.\n";
        workspace = new Workspace();
    }
    system.addChild(workspace);

    // Physics を Workspace にセット
    workspace->setPhysicsEngine(&physicsEngine);

    unsigned int floppa   = renderer.loadTexture("assets/image/floppa2048.jpg"); // back
    unsigned int thecat   = renderer.loadTexture("assets/image/the-cat.png");  // front
    unsigned int saladcat = renderer.loadTexture("assets/image/salad-cat.jpg");// top
    unsigned int smile    = renderer.loadTexture("assets/image/smile.png"); // bottom
    unsigned int bliss    = renderer.loadTexture("assets/image/bliss.jpg"); // right
    unsigned int limabis  = renderer.loadTexture("assets/image/Limabis_logo.png"); // left

    luauEngine.setGlobalInstance(workspace->Name, workspace);
    luauEngine.setGlobalInstance("workspace", workspace); 
    luauEngine.setWorkspace(workspace);

    // キャラクターをスポーン
    user.spawnCharacter();
    workspace->addChild(user.character);
    
    // 顔（smile）を頭の正面に追加
    if (user.head) {
        user.head->addChild(new Decal(smile, Face::Front));
    }
    
    // Freeモード（カメラ操作）がデフォルト
    // user.controlMode = User::ControlMode::Character; // Fキーで切り替え可能

    float lastFrame = static_cast<float>(glfwGetTime());
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        physicsEngine.update(*workspace, deltaTime);
        luauEngine.update(deltaTime);
        
        // Workspace のスクリプトを実行
        luauEngine.executeWorkspaceScripts();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- 2. 入力検知 (向きに基づいた移動) ---
        user.processInput(&physicsEngine);
        if (user.wannaExit) {
            break;
        }

        renderer.render(user, window, *workspace);
        // std::cout << "[DEBUG] Rendered frame" << std::endl;
    }

    glfwTerminate();
    std::cout << "[DEBUG] Main loop ended. wannaExit=" << user.wannaExit << " windowShouldClose=" << glfwWindowShouldClose(window) << std::endl;
    return 0;
}