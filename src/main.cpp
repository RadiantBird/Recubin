#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Math/Matrix4.hpp>

#include <Instances/Cube.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>
#include <Core/AudioService.hpp>

#include <Editor/EditorManager.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Core/SystemState.hpp>

#include <Util/Logger.hpp>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstddef>

#include <PhysX/PxPhysicsAPI.h>
#include <memory>

// ===================================================
//  ウィンドウセットアップ
// ===================================================
GLFWwindow* setupWindow() {
    std::cout << "initing GLFW...\n";
    if (!glfwInit()) {
        std::cout << "GLFW init failed\n";
        return nullptr;
    }

    std::cout << "creating window...\n";
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Recubin Editor", nullptr, nullptr);
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

// ===================================================
//  main
// ===================================================
int main() {
    std::cout << "Hello world!\n"
              << "Recubin Editor v0.80\n";

    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    auto renderer     = std::make_unique<Renderer>();
    auto physics      = std::make_unique<Physics>();
    auto audioService = std::make_unique<AudioService>();
    auto system       = std::make_shared<Instance>("System");
    auto luauEngine   = std::make_unique<LuauEngine>();
    auto user         = std::make_unique<User>(window);

    physics->init();
    renderer->init(window);

    if (!audioService->initialize()) {
        RCBN_LOG("[ERROR] Failed to initialize AudioService.");
        return -1;
    }

    auto workspace = std::static_pointer_cast<Workspace>(
        SceneLoader::loadScene("assets/scenes/test_scene.yaml"));
    if (!workspace) {
        std::cerr << "[ERROR] Failed to load scene. Creating empty workspace.\n";
        workspace = std::make_shared<Workspace>();
    }
    system->addChild(workspace);

    workspace->setPhysicsEngine(physics.get());

    unsigned int floppa   = renderer->loadTexture("assets/image/floppa2048.jpg");
    unsigned int thecat   = renderer->loadTexture("assets/image/the-cat.png");
    unsigned int saladcat = renderer->loadTexture("assets/image/salad-cat.jpg");
    unsigned int smile    = renderer->loadTexture("assets/image/smile.png");
    unsigned int bliss    = renderer->loadTexture("assets/image/bliss.jpg");
    unsigned int limabis  = renderer->loadTexture("assets/image/Limabis_logo.png");

    luauEngine->setGlobalInstance(workspace->Name, workspace.get());
    luauEngine->setGlobalInstance("workspace", workspace.get());
    luauEngine->setWorkspace(workspace.get());

    // ===================================================
    //  EditorManager を Renderer に接続
    // ===================================================
    renderer->editor = std::make_unique<EditorManager>(workspace.get(), user.get());
    RCBN_LOG("Editor initialized.");

    float lastFrame = static_cast<float>(glfwGetTime());
    bool wasPlaying = false;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        SystemState& state = SystemState::get();
        state.isPlaying = renderer->editor && !renderer->editor->isEditMode();
        state.isPaused  = renderer->editor &&  renderer->editor->isPauseMode();

        const bool isPlaying = state.isPlaying;
        const bool isPaused  = state.isPaused;

        // ---- Play/Stop 遷移処理 ----
        if (isPlaying && !wasPlaying) {
            user->spawnCharacter();
            if (user->character) workspace->addChild(user->character);
            if (user->head) user->head->addChild(std::make_shared<Decal>(smile, Face::Front));
        }
        if (!isPlaying && wasPlaying) {
            user->despawnCharacter();
            physics->clearCubes();                // stale ポインタをベクターから除去
            system->removeChild(workspace->Name);
            workspace = std::static_pointer_cast<Workspace>(
                SceneLoader::loadScene("assets/scenes/test_scene.yaml"));
            if (!workspace) workspace = std::make_shared<Workspace>();
            system->addChild(workspace);
            workspace->setPhysicsEngine(physics.get());
            luauEngine->setGlobalInstance(workspace->Name, workspace.get());
            luauEngine->setGlobalInstance("workspace", workspace.get());
            luauEngine->setWorkspace(workspace.get());
            renderer->editor->setWorkspace(workspace.get()); // 全パネルのポインタを一括更新
        }
        wasPlaying = isPlaying;

        // ---- エディターモード中は物理・スクリプトを止める ----
        if (isPlaying && !isPaused) {
            physics->update(*workspace.get(), deltaTime);
            luauEngine->update(deltaTime);
            luauEngine->executeWorkspaceScripts();
        }

        // ---- 入力処理（エディターモードではカメラ操作のみ許可）----
        ViewportPanel* vp = renderer->editor ? renderer->editor->viewportPanel.get() : nullptr;
        state.viewportFocused    = vp && IsViewportFocused(vp);
        state.viewportZoomEnabled = vp && (IsViewportFocused(vp) || vp->isHoveringViewport);
        user->processInput(physics.get());
        if (user->wannaExit) break;

        // ---- 描画 ----
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace.get());

        audioService->updateSounds();
    }

    std::cout << "[DEBUG] Main loop ended.\n";
    RCBN_LOG("Shutting down...");

    glfwTerminate();
    return 0;
}
