#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <include/Math/Matrix4.hpp>

#include <include/Instances/Cube.hpp>
#include <include/Instances/Workspace.hpp>

#include <include/Core/Renderer.hpp>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstddef>

#include <include/PhysX/PxPhysicsAPI.h>

// テスト用のダミー関数
void testPhysX() {
    static physx::PxDefaultAllocator gAllocator;
    static physx::PxDefaultErrorCallback gErrorCallback;

    // Foundation (基盤) を作ってみる
    physx::PxFoundation* foundation = PxCreateFoundation(
        PX_PHYSICS_VERSION, 
        gAllocator, 
        gErrorCallback
    );

    if (foundation) {
        std::cout << "PhysX Foundation created successfully!" << std::endl;
        foundation->release();
    }
}

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
              << "Version 0.3\n";
    
    testPhysX();
    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    User user(window);
    Renderer renderer;
    renderer.init();

    unsigned int floppa   = renderer.loadTexture("assets/image/floppa2048.jpg"); // back
    unsigned int thecat   = renderer.loadTexture("assets/image/the-cat.png");  // front
    unsigned int saladcat = renderer.loadTexture("assets/image/salad-cat.jpg");// top
    unsigned int smile    = renderer.loadTexture("assets/image/smile.png"); // bottom
    unsigned int bliss    = renderer.loadTexture("assets/image/bliss.jpg"); // right
    unsigned int limabis  = renderer.loadTexture("assets/image/Limabis_logo.png"); // left

    Workspace workspace;

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
    floppaCube->Color = Color4(1.0f, 1.0f, 0.0f, 1.0f);
    
    // 各面のテクスチャ設定
    floppaCube->setFaceTexture(0, floppa);
    floppaCube->setFaceTexture(1, thecat);
    floppaCube->setFaceTexture(2, saladcat);
    floppaCube->setFaceTexture(3, smile);
    floppaCube->setFaceTexture(4, bliss);
    floppaCube->setFaceTexture(5, limabis);
    
    workspace.addChild(floppaCube);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);

        // --- 2. 入力検知 (向きに基づいた移動) ---
        user.processInput();
        if (user.wannaExit) {
            break;
        }

        renderer.render(user, window, workspace);
    }

    glfwTerminate();

    return 0;
}