#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Math/Matrix4.hpp>

#include <Instances/Cube.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/CharacterSetting.hpp>
#include <Instances/Decal.hpp>

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
#include "include/stb_image.h"

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
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Recubin Studio", nullptr, nullptr);
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

static CharacterSetting* findCharacterSetting(Instance* inst) {
    if (!inst) return nullptr;
    if (inst->GetClassName() == "CharacterSetting") return static_cast<CharacterSetting*>(inst);
    for (auto& [name, child] : inst->children) {
        if (auto* found = findCharacterSetting(child.get())) return found;
    }
    return nullptr;
}

// AppImage インスタンスを Root から探してウィンドウアイコンを設定する
void applyAppIcon(GLFWwindow* window, Instance* root) {
    if (!window || !root) return;
    for (auto& [name, child] : root->children) {
        if (child->GetClassName() == "AppImage") {
            auto* ai = static_cast<AppImage*>(child.get());
            if (ai->iconPath.empty()) return;
            int w, h, ch;
            stbi_set_flip_vertically_on_load(0);
            unsigned char* px = stbi_load(ai->iconPath.c_str(), &w, &h, &ch, 4);
            stbi_set_flip_vertically_on_load(1);
            if (px) {
                GLFWimage img{ w, h, px };
                glfwSetWindowIcon(window, 1, &img);
                stbi_image_free(px);
            }
            return;
        }
    }
}

// 安全な終了処理関数
bool checkExit(EditorManager* ed, GLFWwindow& window) {
    if (ed && ed->isDirty()) {
        ed->requestSaveDialog(&window);
        glfwSetWindowShouldClose(&window, GLFW_FALSE);
    } else {
        return true;
    }
    return false;
}

// ===================================================
//  main
// ===================================================
int main(int argc, char* argv[]) {
    std::cout << "Hello world!\n"
              << "Recubin Studio v0.98\n";
    std::string engineExePath = (argc > 0 && argv[0]) ? argv[0] : "";

    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    auto renderer     = std::make_unique<Renderer>();
    auto physics      = std::make_unique<Physics>();
    auto audioService = std::make_unique<AudioService>();
    auto system       = std::make_shared<System>();
    auto luauEngine   = std::make_unique<LuauEngine>();
    auto user         = std::make_unique<User>(window);
    user->controlMode = User::ControlMode::Free;

    physics->init();
    renderer->init(window);

    if (!audioService->initialize()) {
        RCBN_LOG("[ERROR] Failed to initialize AudioService.");
        return -1;
    }

    // シングルトンを先に構築して System に接続
    auto workspace = std::make_shared<Workspace>();
    auto lighting  = std::make_shared<Lighting>();
    lighting->Name = "Lighting";
    system->addChild(workspace);
    system->addChild(lighting);
    workspace->setPhysicsEngine(physics.get());

    // YAML が System/Workspace/Lighting を重複生成しないようシングルトン登録してロード
    SceneLoader::registerSingleton("System",    system);
    SceneLoader::registerSingleton("Workspace", workspace);
    SceneLoader::registerSingleton("Lighting",  lighting);
    SceneLoader::loadScene("assets/scenes/test_scene.yaml");
    SceneLoader::clearSingletons();
    applyAppIcon(window, system.get());

    unsigned int floppa   = renderer->loadTexture("assets/image/floppa2048.jpg");
    unsigned int thecat   = renderer->loadTexture("assets/image/the-cat.png");
    unsigned int saladcat = renderer->loadTexture("assets/image/salad-cat.jpg");
    unsigned int smile    = renderer->loadTexture("assets/image/smile.png");
    unsigned int bliss    = renderer->loadTexture("assets/image/bliss.jpg");
    unsigned int limabis  = renderer->loadTexture("assets/image/Limabis_logo.png");
    unsigned int hehe     = renderer->loadTexture("assets/image/hehe.png");
    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setWorkspace(workspace);
    luauEngine->setSystem(system.get());
    physics->onContactCallback = [&](BaseCube* a, BaseCube* b) {
        luauEngine->onCollision(a, b);
    };
    renderer->m_onButtonActivated = [&](GuiButton* btn) {
        luauEngine->onGuiButtonActivated(btn);
    };

    // ===================================================
    //  EditorManager を Renderer に接続
    // ===================================================
    auto editorOwned = std::make_unique<EditorManager>(workspace.get(), user.get(), system.get());
    EditorManager* ed = editorOwned.get();
    ed->engineExePath = engineExePath;
    renderer->editor = std::move(editorOwned);
    RCBN_LOG("Editor initialized.");

    float lastFrame = static_cast<float>(glfwGetTime());
    bool wasPlaying = false;
    bool snapshotDirty = false;
    const std::string snapshotPath = "assets/scenes/_snapshot.yaml";

    // RCBN_LOG("For debbuging, stopping here...");
    // int garbage = 0; // anti optimize
    // std::cin >> garbage;
    // RCBN_LOG(garbage);

    while (true) { // this loop is broken
        if (glfwWindowShouldClose(window)) {
            if (checkExit(ed, *window)) {
                break;
            }
        }
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        SystemState& state = SystemState::get();
        state.isPlaying = ed && !ed->isEditMode();
        state.isPaused  = ed &&  ed->isPauseMode();

        const bool isPlaying = state.isPlaying;
        const bool isPaused  = state.isPaused;

        // ---- Play/Stop 遷移処理 ----
        if (isPlaying && !wasPlaying) {
            snapshotDirty = ed && ed->isDirty();
            SceneLoader::saveScene(system.get(), snapshotPath);
            SceneLoader::resolveConstraintRefs(system.get());
            CharacterSetting* cs = findCharacterSetting(system.get());
            user->spawnCharacter(cs);
            if (cs && !cs->facePath.empty()) {
                unsigned int faceTexID = renderer->loadTexture(cs->facePath.c_str());
                if (faceTexID && user->head)
                    user->head->addChild(std::make_shared<Decal>(faceTexID, Face::Front));
            }
            audioService->playAutoPlaySounds();
            if (user->character) workspace->addChild(user->character);
        }
        if (!isPlaying && wasPlaying) {
            audioService->stopAllSounds();
            user->despawnCharacter();
            physics->clearCubes();
            system->removeChild(workspace->Name);

            auto freshWs = std::make_shared<Workspace>();
            SceneLoader::registerSingleton("Workspace", freshWs);
            SceneLoader::registerSingleton("Lighting",  lighting);
            SceneLoader::loadScene(snapshotPath);
            SceneLoader::clearSingletons();
            workspace = freshWs;

            system->addChild(workspace);
            workspace->setPhysicsEngine(physics.get());
            luauEngine->setGlobalInstance(workspace->Name, workspace);
            luauEngine->setGlobalInstance("workspace", workspace);
            luauEngine->setWorkspace(workspace);
            ed->setWorkspace(workspace.get());
            if (snapshotDirty) ed->markDirty();
        }
        wasPlaying = isPlaying;

        // ---- Load ボタンによるシーンリロード ----
        if (ed && !ed->pendingLoadPath.empty() && ed->isEditMode()) {
            std::string loadPath = ed->pendingLoadPath;
            ed->pendingLoadPath.clear();

            physics->clearCubes();
            system->removeChild(workspace->Name);

            auto freshWs = std::make_shared<Workspace>();
            SceneLoader::registerSingleton("System",    system);
            SceneLoader::registerSingleton("Workspace", freshWs);
            SceneLoader::registerSingleton("Lighting",  lighting);
            SceneLoader::loadScene(loadPath);
            SceneLoader::clearSingletons();
            workspace = freshWs;

            system->addChild(workspace);
            workspace->setPhysicsEngine(physics.get());
            luauEngine->setGlobalInstance(workspace->Name, workspace);
            luauEngine->setGlobalInstance("workspace", workspace);
            luauEngine->setWorkspace(workspace);
            ed->setWorkspace(workspace.get());
            ed->scenePath = loadPath;
            applyAppIcon(window, system.get());
        }

        // ---- エディターモード中は物理・スクリプトを止める ----
        if (isPlaying && !isPaused) {
            physics->update(*workspace.get(), deltaTime);
            luauEngine->fireHeartbeat(deltaTime);
            luauEngine->update(deltaTime);
            luauEngine->executeWorkspaceScripts();
        }

        // ---- 入力処理（エディターモードではカメラ操作のみ許可）----
        ViewportPanel* vp = ed ? ed->viewportPanel.get() : nullptr;
        state.viewportFocused    = vp && IsViewportFocused(vp);
        state.viewportZoomEnabled = vp && (IsViewportFocused(vp) || vp->isHoveringViewport);
        user->processInput(*physics);
        if (user->wannaExit) {
            user->wannaExit = false;
            if (checkExit(ed, *window)) {
                break;
            }
        }

        // ---- 描画 ----
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace.get());

        audioService->updateSounds(user->cpos, user->right);
    }

    std::cout << "[DEBUG] Main loop ended.\n";
    RCBN_LOG("Shutting down...");

    // ---- 明示的クリーンアップ（デストラクタの逆順に依存しない安全な終了）----
    // EditorManager の Undo スタックや Clipboard が BaseCube の shared_ptr を
    // 保持している可能性がある。これらが renderer の破棄時（physics 破棄後）に
    // 解放されると、BaseCube のデストラクタで lastWorkspace->physicsEngine に
    // アクセスしてクラッシュする。Physics がまだ生きている今のうちにクリアする。
    if (ed) {
        ed->hierarchyPanel->selectedInstance = nullptr;
        ed->m_history.clear();
        ed->clearClipboard();
    }
    physics->clearCubes();
    workspace->setPhysicsEngine(nullptr);
    system->removeChild(workspace->Name);
    workspace.reset();
    system.reset();

    glfwTerminate();
    return 0;
}
