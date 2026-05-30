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

static std::vector<std::shared_ptr<Workspace>> collectWorkspaces(const std::shared_ptr<System>& system) {
    std::vector<std::shared_ptr<Workspace>> result;
    if (!system) return result;

    for (auto& [name, child] : system->getChildren()) {
        if (child && child->IsA("Workspace")) {
            result.push_back(std::static_pointer_cast<Workspace>(child));
        }
    }
    return result;
}

static void clearWorkspacePhysics(const std::vector<std::shared_ptr<Workspace>>& workspaces) {
    for (auto& ws : workspaces) {
        if (ws && ws->getPhysicsEngine()) {
            ws->getPhysicsEngine()->clearCubes();
        }
    }
}

static void removeWorkspacesFromSystem(
    const std::shared_ptr<System>& system,
    const std::vector<std::shared_ptr<Workspace>>& workspaces)
{
    if (!system) return;

    for (auto& ws : workspaces) {
        if (ws) {
            system->removeChild(ws->Name);
        }
    }
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
              << "Recubin Studio v0.985\n";
    std::string engineExePath = (argc > 0 && argv[0]) ? argv[0] : "";

    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    auto renderer     = std::make_unique<Renderer>();
    auto audioService = std::make_unique<AudioService>();
    auto system       = std::make_shared<System>();
    auto luauEngine   = std::make_unique<LuauEngine>();
    auto user         = std::make_unique<User>(window);
    user->controlMode = User::ControlMode::Free; // エディターではフリーモードから開始(パッケージされたゲームランタイムはCharacterモードから開始)

    renderer->init(window);

    if (!audioService->initialize()) {
        RCBN_LOG("[ERROR] Failed to initialize AudioService.");
        return -1;
    }

    // s_contactCallback を設定（全Physicsインスタンスで共有）
    Physics::s_contactCallback = [&](BaseCube* a, BaseCube* b) {
        luauEngine->onCollision(a, b);
    };

    std::vector<std::shared_ptr<Workspace>> workspaces;
    std::shared_ptr<Workspace> workspace;

    // Register only System so YAML can keep multiple Workspace nodes.
    SceneLoader::registerSingleton("System", system);

    SceneLoader::loadScene("assets/scenes/test_scene.yaml");
    SceneLoader::clearSingletons();

    workspaces = collectWorkspaces(system);
    if (workspaces.empty()) {
        workspace = std::make_shared<Workspace>();
        auto lighting = std::make_shared<Lighting>();
        lighting->Name = "Lighting";
        system->addChild(workspace);
        workspace->addChild(lighting);
        workspaces = collectWorkspaces(system);
    }
    workspace = workspaces.front();
    workspace->initPhysics();

    // 古い形式のYAML対応: System直下のLightingを見つけたら、WorkspaceのLightingにプロパティを移して削除
    for (auto it = system->children.begin(); it != system->children.end(); ) {
        if (it->second->IsA("Lighting")) {
            auto oldLighting = std::static_pointer_cast<Lighting>(it->second);
            auto lighting = std::make_shared<Lighting>();
            lighting->Name = oldLighting->Name;
            lighting->lightDir = oldLighting->lightDir;
            lighting->brightness = oldLighting->brightness;
            it = system->children.erase(it);
            workspace->addChild(lighting);
            break;
        } else {
            ++it;
        }
    }

    applyAppIcon(window, system.get());

    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setGlobalInstance("system", system);
    luauEngine->setWorkspace(workspace);
    luauEngine->setSystem(system.get());
    renderer->m_onButtonActivated = [&](GuiButton* btn) {
        luauEngine->onGuiButtonActivated(btn);
    };

    // ===================================================
    //  EditorManager を Renderer に接続
    // ===================================================
    auto editorOwned = std::make_unique<EditorManager>(workspace.get(), user.get(), system.get());
    EditorManager* ed = editorOwned.get();
    ed->engineExePath = engineExePath;

    // Workspace 切り替えコールバックを設定
    ed->hierarchyPanel->onSwitchWorkspace = [&](Workspace* ws) {
        auto wsSp = std::static_pointer_cast<Workspace>(ws->shared_from_this());
        workspaces = collectWorkspaces(system);
        workspace = wsSp;
        luauEngine->setGlobalInstance("workspace", workspace);
        luauEngine->setWorkspace(workspace);
        ed->setWorkspace(workspace.get());
    };
    ed->hierarchyPanel->onOpenSecondaryViewport = [&](Workspace* ws) {
        ed->openSecondaryViewport(ws);
    };
    renderer->editor = std::move(editorOwned);
    RCBN_LOG("Editor initialized.");

    float lastFrame = static_cast<float>(glfwGetTime());
    bool wasPlaying = false;
    bool snapshotDirty = false;
    const std::string snapshotPath = "assets/scenes/_snapshot.yaml";

    auto initNewScene = [&](const std::string& path, bool isDirty) {
        SceneLoader::registerSingleton("System", system);
        SceneLoader::loadScene(path);
        SceneLoader::clearSingletons();

        workspaces = collectWorkspaces(system);
        if (workspaces.empty()) {
            workspace = std::make_shared<Workspace>();
            system->addChild(workspace);
            workspaces = collectWorkspaces(system);
        }
        workspace = workspaces.front();
        luauEngine->setGlobalInstance(workspace->Name, workspace);
        luauEngine->setGlobalInstance("workspace", workspace);
        luauEngine->setWorkspace(workspace);
        ed->setWorkspace(workspace.get());
        if (isDirty) ed->markDirty();
        
        workspace->initPhysics();
    };

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
            // 全WorkspaceのPhysicsを初期化
            for (auto& [name, child] : system->getChildren()) {
                if (child->IsA("Workspace")) {
                    auto* ws = static_cast<Workspace*>(child.get());
                    if (!ws->getPhysicsEngine()) ws->initPhysics();
                }
            }
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
            // 全Workspaceのクリア（ownedPhysics デストラクタで自動解放）
            workspaces = collectWorkspaces(system);
            clearWorkspacePhysics(workspaces);
            removeWorkspacesFromSystem(system, workspaces);

            initNewScene(snapshotPath, snapshotDirty);
        }
        wasPlaying = isPlaying;

        // ---- Load ボタンによるシーンリロード ----
        if (ed && !ed->pendingLoadPath.empty() && ed->isEditMode()) {
            std::string loadPath = ed->pendingLoadPath;
            ed->pendingLoadPath.clear();

            workspaces = collectWorkspaces(system);
            clearWorkspacePhysics(workspaces);
            removeWorkspacesFromSystem(system, workspaces);

            initNewScene(loadPath, false);
            ed->scenePath = loadPath;
            applyAppIcon(window, system.get());
        }

        // ---- エディターモード中は物理・スクリプトを止める ----
        if (isPlaying && !isPaused) {
            for (auto& [name, child] : system->getChildren()) {
                if (!child->IsA("Workspace")) continue;
                auto* ws = static_cast<Workspace*>(child.get());
                if (!ws->getPhysicsEngine()) ws->initPhysics();
                luauEngine->executeWorkspaceScripts(*ws);
                ws->getPhysicsEngine()->update(*ws, deltaTime);
            }
            luauEngine->fireHeartbeat(deltaTime);
            luauEngine->update(deltaTime);
        }

        // ---- 入力処理（エディターモードではカメラ操作のみ許可）----
        ViewportPanel* focusedVP = ed ? GetFocusedViewport() : nullptr;
        state.viewportFocused    = focusedVP != nullptr;
        state.viewportZoomEnabled = focusedVP != nullptr || (ed ? ed->isAnyViewportHovered() : false);
        user->processInput(workspace->getPhysicsEngine());
        if (user->wannaExit) {
            user->wannaExit = false;
            if (checkExit(ed, *window)) {
                break;
            }
        }

        // ---- Pキー: Workspace 切り替え ----
        if (user->wantsSwitchWorkspace && isPlaying) {
            user->wantsSwitchWorkspace = false;
            // System直下のWorkspaceリストを収集
            std::vector<Workspace*> workspacePtrs;
            workspaces = collectWorkspaces(system);
            for (auto& ws : workspaces) {
                if (ws) workspacePtrs.push_back(ws.get());
            }
            if (workspacePtrs.size() > 1) {
                auto it = std::find(workspacePtrs.begin(), workspacePtrs.end(), workspace.get());
                Workspace* next = (it != workspacePtrs.end() && std::next(it) != workspacePtrs.end())
                    ? *std::next(it) : workspacePtrs.front();
                if (next != workspace.get()) {
                    // キャラクターを新Workspaceに移動（ワールド座標維持）
                    if (user->character) {
                        Vector3 worldPos = user->character->getWorldPosition();
                        auto charSp = std::static_pointer_cast<Instance>(user->character);
                        workspace->removeChild(user->character->Name);
                        next->addChild(charSp);
                        user->character->Position = worldPos;
                    }
                    // activeWorkspace 更新
                    workspace = std::static_pointer_cast<Workspace>(next->shared_from_this());
                    luauEngine->setGlobalInstance("workspace", workspace);
                    luauEngine->setWorkspace(workspace);
                    ed->setWorkspace(workspace.get());
                }
            }
        }
        user->wantsSwitchWorkspace = false;

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
    // 全WorkspaceのPhysicsをクリア（m_ownedPhysics デストラクタで PxScene 解放）
    workspaces = collectWorkspaces(system);
    for (auto& ws : workspaces) {
        if (ws && ws->getPhysicsEngine()) {
            ws->getPhysicsEngine()->clearCubes();
            ws->setPhysicsEngine(nullptr);
        }
    }
    removeWorkspacesFromSystem(system, workspaces);
    workspace.reset();
    workspaces.clear();
    system.reset();

    glfwTerminate();
    return 0;
}
