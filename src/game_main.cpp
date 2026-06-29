#include <windows26.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Decal.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/AudioService.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <Core/SystemState.hpp>
#include <Editor/NullEditorManager.hpp>
#include <include/imgui/imgui.h>

#include <Util/Logger.hpp>
#include <yaml-cpp/yaml.h>
#include "include/stb_image.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>

// ===================================================
//  startup.yaml からゲーム設定を読み込む
// ===================================================
struct GameConfig {
    std::string gameName  = "Recubin Game";
    std::string startScene = "assets/scenes/game.yaml";
    bool debugLog = false; // ランタイムのコンソールを表示するかどうか（未実装）
    // todo: debugLogの書き込み処理を追加しておく
};

static GameConfig loadStartup() {
    GameConfig cfg;
    try {
        std::ifstream f("startup.yaml");
        if (!f.is_open()) return cfg;
        std::stringstream ss;
        ss << f.rdbuf();
        YAML::Node node = YAML::Load(ss.str());
        if (node["GameName"])   cfg.gameName   = node["GameName"].as<std::string>();
        if (node["StartScene"]) cfg.startScene = node["StartScene"].as<std::string>();
        if (node["DebugLog"])   cfg.debugLog   = node["DebugLog"].as<bool>();
    } catch (...) {}
    return cfg;
}


static void applyAppIcon(GLFWwindow* window, Instance* root) {
    if (!window || !root) return;
    for (auto& [name, child] : root->children) {
        if (child->getClassName() == "AppImage") {
            auto* ai = static_cast<AppImage*>(child.get());
            if (ai->iconPath.empty()) return;
            int w, h, ch;
            // GLFW/Windows アイコンは左上原点なので flip 不要
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

// ===================================================
//  main
// ===================================================
int main() {
    // コンソールの出力/入力コードページをUTF-8にする
    // (Windows日本語版等では既定のANSIコードページのままだと、UTF-8で書かれた
    //  ログやLuauのprint出力が文字化けする)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    GameConfig cfg = loadStartup();

    // ---- ウィンドウ作成 ----
    if (!glfwInit()) return -1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, cfg.gameName.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    if (glewInit() != GLEW_OK) return -1;

    // ---- コアシステム初期化 ----
    auto renderer     = std::make_unique<Renderer>();
    auto audioService = std::make_unique<AudioService>();
    auto system       = std::make_shared<System>();
    auto luauEngine   = std::make_unique<LuauEngine>();
    auto user         = std::make_shared<User>(std::make_unique<GLFWInputBackend>(window));
    user->controlMode = User::ControlMode::Character;

    renderer->init(window);
    renderer->editor = std::make_unique<NullEditorManager>();

    if (!audioService->initialize()) {
        RCBN_LOG("[ERROR] Failed to initialize AudioService.");
        return -1;
    }

    // ---- シーンのロード（Workspaceはシングルトン登録しない = 複数ワークスペース対応） ----
    SceneLoader::registerSingleton("System", system);
    SceneLoader::loadScene(cfg.startScene);
    SceneLoader::clearSingletons();

    // System直下のWorkspaceを収集するローカルヘルパ
    auto collectWorkspaces = [](const std::shared_ptr<System>& sys) {
        std::vector<std::shared_ptr<Workspace>> result;
        for (auto& [name, child] : sys->children) {
            if (child && child->IsA("Workspace"))
                result.push_back(std::static_pointer_cast<Workspace>(child));
        }
        return result;
    };
    std::vector<std::shared_ptr<Workspace>> workspaces = collectWorkspaces(system);

    // シーンにWorkspaceが無ければデフォルト生成
    if (workspaces.empty()) {
        auto ws = std::make_shared<Workspace>();
        auto li = std::make_shared<Lighting>();
        li->Name = "Lighting";
        system->addChild(ws);
        ws->addChild(li);
        workspaces = collectWorkspaces(system);
    }
    std::shared_ptr<Workspace> workspace = workspaces.front();

    // 全Workspaceの物理初期化
    for (auto& ws : workspaces) {
        if (!ws->getPhysicsEngine()) ws->initPhysics();
    }

    // 古い形式のYAML対応: System直下のLightingを見つけたら、WorkspaceのLightingにプロパティを移して削除
    for (auto it = system->children.begin(); it != system->children.end(); ) {
        if (it->second->IsA("Lighting")) {
            auto oldLighting = std::static_pointer_cast<Lighting>(it->second);
            for (auto& [n, child] : workspace->children) {
                if (child->IsA("Lighting")) {
                    auto wsLighting = std::static_pointer_cast<Lighting>(child);
                    wsLighting->lightDir   = oldLighting->lightDir;
                    wsLighting->brightness = oldLighting->brightness;
                    break;
                }
            }
            it = system->children.erase(it);
            break;
        } else {
            ++it;
        }
    }

    applyAppIcon(window, system.get());

    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setGlobalInstance("User", user);
    luauEngine->setWorkspace(workspace);
    luauEngine->setSystem(system.get());
    Physics::s_contactCallback = [&](BaseCube* a, BaseCube* b) {
        luauEngine->onCollision(a, b);
    };
    renderer->m_onButtonActivated = [&](GuiButton* btn) {
        luauEngine->onGuiButtonActivated(btn);
    };

    // ---- ゲーム開始 ----
    user->spawnCharacter(system.get());
    audioService->playAutoPlaySounds();
    if (user->character) workspace->addChild(user->character);

    // ランタイムは常にプレイ状態。エディターと違いフラグを設定する箇所が無いため明示する
    // （ProximityPrompt 表示・キー判定・GUIボタンのクリック・物理の enqueue 経路に必要）
    SystemState::get().isPlaying       = true;
    SystemState::get().viewportFocused = true;
    SystemState::get().inputState      = InputState::Gameplay;

    float lastFrame = static_cast<float>(glfwGetTime());

    // ---- メインループ（常にプレイ状態） ----
    while (!glfwWindowShouldClose(window)) {
        float now       = static_cast<float>(glfwGetTime());
        float deltaTime = now - lastFrame;
        lastFrame       = now;

        if (workspace->getPhysicsEngine()) workspace->getPhysicsEngine()->update(*workspace, deltaTime);
        luauEngine->fireHeartbeat(deltaTime);
        luauEngine->update(deltaTime);
        luauEngine->executeWorkspaceScripts(*workspace);

        // エディタが存在しないため、常にゲームプレイ入力として扱う
        user->processInput(workspace->getPhysicsEngine(), deltaTime,
                            /*viewportFocused=*/true, /*viewportZoomEnabled=*/true,
                            /*isGameplayInput=*/true,
                            ImGui::GetIO().WantTextInput);
        if (user->consumeExitRequest()) break;

        // ---- Pキー: Workspace切り替え ----
        if (user->consumeWorkspaceSwitchRequest()) {
            workspaces = collectWorkspaces(system);
            std::vector<Workspace*> ptrs;
            for (auto& ws : workspaces) ptrs.push_back(ws.get());
            if (ptrs.size() > 1) {
                auto it = std::find(ptrs.begin(), ptrs.end(), workspace.get());
                Workspace* next = (it != ptrs.end() && std::next(it) != ptrs.end())
                    ? *std::next(it) : ptrs.front();
                if (next != workspace.get()) {
                    if (user->character) {
                        Vector3 worldPos = user->character->getWorldPosition();
                        auto charSp = std::static_pointer_cast<Instance>(user->character);
                        workspace->removeChild(user->character->Name);
                        next->addChild(charSp);
                        user->character->Position = worldPos;
                    }
                    workspace = std::static_pointer_cast<Workspace>(next->shared_from_this());
                    if (!workspace->getPhysicsEngine()) workspace->initPhysics();
                    luauEngine->setGlobalInstance("workspace", workspace);
                    luauEngine->setWorkspace(workspace);
                }
            }
        }

        // Humanoidのパーツ配置(processInput内のapplyBodyAnimation)が終わった直後に、
        // アンカー駆動のキネマティックWeld(帽子等)を即時同期して追従ラグを無くす
        if (workspace->getPhysicsEngine()) workspace->getPhysicsEngine()->syncWeldKinematics();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace);

        audioService->updateSounds(user->cpos, user->right);
    }

    // ---- クリーンアップ ----
    for (auto& ws : workspaces) {
        if (ws && ws->getPhysicsEngine()) {
            ws->getPhysicsEngine()->clearCubes();
            ws->setPhysicsEngine(nullptr);
        }
    }
    workspace.reset();
    workspaces.clear();
    system.reset();

    glfwTerminate();
    return 0;
}
