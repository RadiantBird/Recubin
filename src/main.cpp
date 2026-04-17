#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Math/Matrix4.hpp>

#include <Instances/Cube.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Script.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>

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
              << "Version 0.71\n";
    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    User user(window);
    Renderer renderer;
    Physics physicsEngine;
    LuauEngine luauEngine;

    physicsEngine.init();
    renderer.init();

    unsigned int floppa   = renderer.loadTexture("assets/image/floppa2048.jpg"); // back
    unsigned int thecat   = renderer.loadTexture("assets/image/the-cat.png");  // front
    unsigned int saladcat = renderer.loadTexture("assets/image/salad-cat.jpg");// top
    unsigned int smile    = renderer.loadTexture("assets/image/smile.png"); // bottom
    unsigned int bliss    = renderer.loadTexture("assets/image/bliss.jpg"); // right
    unsigned int limabis  = renderer.loadTexture("assets/image/Limabis_logo.png"); // left

    Workspace workspace;

    // Physics を Workspace にセット
    workspace.setPhysicsEngine(&physicsEngine);

    // 1. Blue Cube (元 world[0])
    Cube* blueCube = new Cube({0.0f, 0.0f, -2.0f}, {1.0f, 4.0f, 1.0f}, renderer.whiteTexture);
    blueCube->Name = "Blue";
    blueCube->Color = Color4(0.0f, 0.0f, 1.0f, 1.0f);
    workspace.addChild(blueCube);

    // 2. Red Cube (元 world[1])
    Cube* redCube = new Cube({2.0f, 0.0f, -4.0f}, {1.0f, 1.0f, 1.0f}, renderer.whiteTexture);
    redCube->Name = "Red";
    redCube->Color = Color4(1.0f, 0.0f, 0.0f, 1.0f);
    workspace.addChild(redCube);

    // 3. Green Cube (元 world[2])
    Cube* greenCube = new Cube({-2.0f, 0.0f, -4.0f}, {2.0f, 1.0f, 1.0f}, renderer.whiteTexture);
    greenCube->Name = "Green";
    greenCube->Color = Color4(0.0f, 1.0f, 0.0f, 1.0f);
    workspace.addChild(greenCube);

    // 4. Custom Textured Cube (元 world[3] / Floppa)
    Cube* floppaCube = new Cube({0.0f, 0.0f, -8.0f}, {2.0f, 2.0f, 2.0f}, renderer.whiteTexture);
    floppaCube->Name = "Floppa";
    floppaCube->Color = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    
    // 各面のテクスチャ設定
    floppaCube->setFaceTexture(0, floppa);
    floppaCube->setFaceTexture(1, thecat);
    floppaCube->setFaceTexture(2, saladcat);
    floppaCube->setFaceTexture(3, smile);
    floppaCube->setFaceTexture(4, bliss);
    floppaCube->setFaceTexture(5, limabis);
    
    workspace.addChild(floppaCube);
    
    luauEngine.setGlobalInstance(workspace.Name, &workspace);
    luauEngine.setGlobalInstance("workspace", &workspace); // Add lower-case alias
    // Workspace を LuauEngine に設定
    luauEngine.setWorkspace(&workspace);
    
    // テスト用スクリプト
    Script* script = new Script("scripts/test.luau");
    script->Name = "TestScript";

    workspace.addChild(script);

    Cube* baseplate = new Cube({0.0f, -10.0f, 0.0f},  {32.0f, 1.0f, 32.0f}, renderer.whiteTexture);
    baseplate->Name = "Baseplate";
    baseplate->Color = Color4(0.0f, 1.0f, 0.5f, 1.0f);
    baseplate->Anchored = true;
    workspace.addChild(baseplate);

    // キャラクターをスポーン
    user.spawnCharacter();
    workspace.addChild(user.character);
    
    // Freeモード（カメラ操作）がデフォルト
    // user.controlMode = User::ControlMode::Character; // Fキーで切り替え可能

    float lastFrame = static_cast<float>(glfwGetTime());
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        physicsEngine.update(workspace, deltaTime);
        luauEngine.update(deltaTime);
        
        // Workspace のスクリプトを実行
        luauEngine.executeWorkspaceScripts();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- 2. 入力検知 (向きに基づいた移動) ---
        user.processInput();
        if (user.wannaExit) {
            break;
        }

        renderer.render(user, window, workspace);
        // std::cout << "[DEBUG] Rendered frame" << std::endl;
    }

    glfwTerminate();
    std::cout << "[DEBUG] Main loop ended. wannaExit=" << user.wannaExit << " windowShouldClose=" << glfwWindowShouldClose(window) << std::endl;
    return 0;
}