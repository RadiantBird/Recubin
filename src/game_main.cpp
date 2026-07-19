#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Decal.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Weather.hpp>
#include <Instances/Humanoid.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/SceneRuntime.hpp>
#include <Core/AudioService.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <Core/SystemState.hpp>
#include <Editor/NullEditorManager.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/Replication.hpp>
#include <include/imgui/imgui.h>

#include <Util/Logger.hpp>
#include <Util/FrameProfiler.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <yaml-cpp/yaml.h>
#include "include/stb_image.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

// ===================================================
//  startup.yaml からゲーム設定を読み込む
// ===================================================
struct GameConfig {
    std::string gameName  = "Recubin Game";
    std::string startScene = "assets/scenes/game.yaml";
    bool debugLog = false; // ランタイムのコンソールを表示するかどうか（未実装）
    // todo: debugLogの書き込み処理を追加しておく
};

// ===================================================
//  v2.0 ネットワーク基盤(基盤tierのモック): --host [port] / --connect <address> [port]
//  でHost/Client役を切り替える。引数なしなら従来通りNetworkRole::Offlineのまま動作する。
// ===================================================
static constexpr uint16_t kDefaultNetworkPort = 7777;

struct NetworkLaunchArgs {
    bool asHost = false;
    bool asClient = false;
    std::string address;
    uint16_t port = kDefaultNetworkPort;
    uint16_t listenPort = 0;
};

static NetworkLaunchArgs parseNetworkArgs(int argc, char* argv[]) {
    NetworkLaunchArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host") {
            args.asHost = true;
            if (i + 1 < argc) {
                try { args.port = static_cast<uint16_t>(std::stoi(argv[i + 1])); ++i; } catch (...) {}
            }
        } else if (arg == "--connect") {
            args.asClient = true;
            if (i + 1 < argc) { args.address = argv[i + 1]; ++i; }
            if (i + 1 < argc) {
                std::string maybePort = argv[i + 1];
                if (!maybePort.empty() && maybePort[0] != '-') {
                    try { args.port = static_cast<uint16_t>(std::stoi(maybePort)); ++i; } catch (...) {}
                }
            }
        } else if (arg == "--listen-port") {
            if (i + 1 < argc) {
                try { args.listenPort = static_cast<uint16_t>(std::stoi(argv[i + 1])); ++i; } catch (...) {}
            }
        }
    }
    if (args.listenPort == 0) args.listenPort = args.port;
    return args;
}

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



// ===================================================
//  main
// ===================================================
int main(int argc, char* argv[]) {
    // コンソールの出力/入力コードページをUTF-8にする
    // (Windows日本語版等では既定のANSIコードページのままだと、UTF-8で書かれた
    //  ログやLuauのprint出力が文字化けする)
    getPlatform().setupConsoleUtf8();

    GameConfig cfg = loadStartup();

    // ---- v2.0 ネットワーク基盤(モック): CLI引数でHost/Client役を起動する ----
    NetworkLaunchArgs netArgs = parseNetworkArgs(argc, argv);
    if (netArgs.asHost) {
        NetworkManager::get().startHost(netArgs.port);
    } else if (netArgs.asClient) {
        NetworkManager::get().connect(netArgs.address, netArgs.port, netArgs.listenPort);
    }

    // ランタイムはゲームフォルダ(cwd)外のアセット読み込みを禁止する（配布ゲームのサンドボックス）。
    // エディター(Recubin.exe)はこれを呼ばないため従来通り任意パスを扱える。
    AssetGuard::enableSandbox(std::filesystem::current_path());

    // ---- ウィンドウ作成 ----
    if (!glfwInit()) return -1;
    // Mac対応: OpenGL 4.1 Core Profileを明示指定（macOSは未指定だとレガシー2.1しか得られない）
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, cfg.gameName.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE; // Core Profileで必要
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

    // ---- シーンのロード（共通初期化） ----
    auto bound     = SceneRuntime::loadAndBind(cfg.startScene, system, user, *luauEngine, window);
    auto workspaces = bound.workspaces;
    auto workspace  = bound.workspace;

    // シーンの User ノード（ControlMode: Free 等）がマージで上書きするため、
    // ランタイムは常にキャラクター操作へ再強制する。
    user->controlMode = User::ControlMode::Character;

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
    ReplicationManager replication(workspace, user, system.get());
    NetworkManager::get().onGameMessage = [&](uint8_t type, const uint8_t* payload, size_t len, PeerId senderId) {
        replication.onGameMessage(type, payload, len, senderId);
    };

    Physics::s_contactCallback = [&](BaseCube* a, BaseCube* b) {
        luauEngine->onCollision(a, b);
    };
    NetworkManager::get().onRoleChanged = [&](NetworkRole oldRole, NetworkRole newRole) {
        RCBN_LOG("NetworkManager: role changed " << NetworkManager::roleToString(oldRole)
                  << " -> " << NetworkManager::roleToString(newRole));
        luauEngine->fireNetworkRoleChanged(oldRole, newRole);
        replication.onNetworkRoleChanged(oldRole, newRole);
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

    // ---- v2.0 ネットワーク基盤(モック)デモ用の状態 ----
    bool  netWasConnected  = false;

    // ---- メインループ（常にプレイ状態） ----
    while (!glfwWindowShouldClose(window)) {
        float now       = static_cast<float>(glfwGetTime());
        float deltaTime = now - lastFrame;
        lastFrame       = now;

        // ---- ネットワークポーリング（物理更新より前＝受信内容を反映してからシミュレートする） ----
        NetworkManager::get().update(deltaTime);
        user->peerId = NetworkManager::get().isActive() ? NetworkManager::get().getLocalPeerId() : 0;
        if (NetworkManager::get().isActive()) {
            bool nowConnected = NetworkManager::get().hasPeers();
            if (nowConnected && !netWasConnected) {
                std::string who = (NetworkManager::get().getRole() == NetworkRole::Host) ? "Host" : "Client";
                NetworkManager::get().sendChatMessage("Hello from " + who);
            }
            netWasConnected = nowConnected;
        }

        // レプリケーション(受信姿勢の適用と自姿勢の送信)。物理更新より前に行う
        replication.update(deltaTime, workspace->getPhysicsEngine());

        FrameProfiler::get().beginSection("physics");
        if (workspace->getPhysicsEngine()) workspace->getPhysicsEngine()->update(*workspace, deltaTime);
        FrameProfiler::get().endSection("physics");
        FrameProfiler::get().beginSection("luau");
        luauEngine->resetFrameSafetyCounters();
        luauEngine->fireHeartbeat(deltaTime);
        luauEngine->update(deltaTime);
        luauEngine->executeWorkspaceScripts(*workspace);
        luauEngine->executeSystemScripts();
        FrameProfiler::get().endSection("luau");
        if (luauEngine->consumeSafetyHaltRequest()) break; // 既存のconsumeExitRequestと同じglfwTerminate()クリーンアップ経路に合流

        // エディタが存在しないため、常にゲームプレイ入力として扱う
        user->processInput(workspace->getPhysicsEngine(), deltaTime,
                            /*viewportFocused=*/true, /*viewportZoomEnabled=*/true,
                            /*isGameplayInput=*/true,
                            ImGui::GetIO().WantTextInput);
        if (user->consumeExitRequest()) break;

        // ---- Pキー: Workspace切り替え ----
        if (user->consumeWorkspaceSwitchRequest()) {
            workspaces = SceneRuntime::collectWorkspaces(system);
            std::vector<Workspace*> ptrs;
            for (auto& ws : workspaces) ptrs.push_back(ws.get());
            if (ptrs.size() > 1) {
                auto it = std::find(ptrs.begin(), ptrs.end(), workspace.get());
                Workspace* next = (it != ptrs.end() && std::next(it) != ptrs.end())
                    ? *std::next(it) : ptrs.front();
                if (next != workspace.get()) {
                    if (user->character) {
                        // 旧Workspaceで Weld を一旦同期し、アンカー(Head)と非アンカー(帽子等)の
                        // ワールド姿勢を同一瞬間で揃えてから移す。これをしないと移動先の rebuildGroup が
                        // 1フレームずれた姿勢からオフセットを再計算し、Weldメンバーが毎スイッチ離れていく。
                        if (workspace->getPhysicsEngine()) workspace->getPhysicsEngine()->syncWeldKinematics();
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

        // 再生中のAnimationを評価し、対象Cubeのcframeを上書きする(main.cppの対応処理と同じ)
        // workspace内の全Humanoid(NPC含む)が対象
        Humanoid::updateAll(workspace.get(), deltaTime);

        // Humanoidのパーツ配置(processInput内のapplyBodyAnimation)が終わった直後に、
        // アンカー駆動のキネマティックWeld(帽子等)を即時同期して追従ラグを無くす
        if (workspace->getPhysicsEngine()) workspace->getPhysicsEngine()->syncWeldKinematics();

        Weather::updateAll(workspace.get(), deltaTime, user->cpos);
        ParticleEmitter::updateAll(workspace.get(), deltaTime);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace);

        audioService->updateSounds(user->cpos, user->right);
        FrameProfiler::get().endFrame();
    }

    // ---- クリーンアップ ----
    NetworkManager::get().shutdown();
    NetworkManager::get().onRoleChanged = nullptr;
    NetworkManager::get().onGameMessage = nullptr;
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
