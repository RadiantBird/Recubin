#include <GL/glew.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
    #include <windows26.h>
#endif

#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Decal.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Weather.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/ChatService.hpp>
#include <Instances/PathfindingService.hpp>

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
#include <Network/NetworkIdentity.hpp>
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
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

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

struct ConsoleChatQueue {
    std::mutex mutex;
    std::deque<std::string> lines;
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
    getPlatform().setupDllSearchPath();

    GameConfig cfg = loadStartup();

    // ---- v2.0 ネットワーク基盤(モック): CLI引数でHost/Client役を起動する ----
    NetworkLaunchArgs netArgs = parseNetworkArgs(argc, argv);
    // System.UseNetwork is loaded from the scene, so networking starts after loadAndBind.

    // 標準入力はblockingなので、レンダー/ネットワークスレッドから分離する。
    // readerはゲーム本体を参照せずshared queueだけを所有するため、getline待ちのまま
    // ウィンドウを閉じてもjoinで終了を妨げない。
    std::shared_ptr<ConsoleChatQueue> consoleChat;
    if (netArgs.asHost || netArgs.asClient) {
        consoleChat = std::make_shared<ConsoleChatQueue>();
        std::thread([queue = consoleChat]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::lock_guard<std::mutex> lock(queue->mutex);
                queue->lines.push_back(std::move(line));
            }
        }).detach();
        std::cout << "[Chat] Type a message and press Enter (maximum 512 UTF-8 bytes)." << std::endl;
    }

    // ランタイムはゲームフォルダ(cwd)外のアセット読み込みを禁止する（配布ゲームのサンドボックス）。
    // エディター(Recubin.exe)はこれを呼ばないため従来通り任意パスを扱える。
    AssetGuard::enableSandbox(std::filesystem::current_path());

    // ---- ウィンドウ作成 ----
#ifdef _WIN32
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
#endif
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
    auto runtimeEditor = std::make_unique<NullEditorManager>();
    NullEditorManager* runtimeEditorPtr = runtimeEditor.get();
    renderer->editor = std::move(runtimeEditor);

    if (!audioService->initialize()) {
        RCBN_LOG("[ERROR] Failed to initialize AudioService.");
        return -1;
    }

    // ---- シーンのロード（共通初期化） ----
    auto bound     = SceneRuntime::loadAndBind(cfg.startScene, system, user, *luauEngine, window);
    auto workspaces = bound.workspaces;
    auto workspace  = bound.workspace;

    // Networked games do not start scripts, character spawning, physics, or replication
    // until the host-authoritative PeerId is known.
    if (system->UseNetwork && netArgs.asHost) {
        if (!NetworkManager::get().startHost(netArgs.port)) {
            glfwTerminate();
            return -1;
        }
    } else if (system->UseNetwork && netArgs.asClient) {
        NetworkManager::get().connect(netArgs.address, netArgs.port, netArgs.listenPort);
        double retryAt = glfwGetTime() + 5.0;
        double lastTick = glfwGetTime();
        while (!glfwWindowShouldClose(window) && NetworkManager::get().getLocalPeerId() == 0) {
            const double now = glfwGetTime();
            NetworkManager::get().update(static_cast<float>(now - lastTick));
            lastTick = now;
            glfwPollEvents();
            if (now >= retryAt && NetworkManager::get().getLocalPeerId() == 0) {
                RCBN_WARN("NetworkManager: Welcome timeout; retrying initial connection");
                NetworkManager::get().connect(netArgs.address, netArgs.port, netArgs.listenPort);
                retryAt = now + 5.0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (glfwWindowShouldClose(window)) {
            NetworkManager::get().shutdown();
            glfwTerminate();
            return 0;
        }
    }

    if (system->UseNetwork && (netArgs.asHost || netArgs.asClient)) {
        const PeerId id = NetworkManager::get().getLocalPeerId();
        if (id == 0 || !user->applyNetworkIdentity(id)) {
            RCBN_ERROR("Failed to apply canonical local network identity");
            NetworkManager::get().shutdown();
            glfwTerminate();
            return -1;
        }
        const std::string characterName = NetworkIdentity::characterName(id);
        if (workspace->children.contains(characterName)) {
            RCBN_ERROR("Canonical local character collision for " << characterName);
            NetworkManager::get().shutdown();
            glfwTerminate();
            return -1;
        }
    }
    std::shared_ptr<ChatService> chatService;
    if (auto it = system->children.find("ChatService"); it != system->children.end()) {
        chatService = std::dynamic_pointer_cast<ChatService>(it->second);
    }
    if (chatService) {
        chatService->onSendRequested = [](const std::string& text) {
            NetworkManager::get().sendChatMessage(text);
        };
        renderer->setChatService(chatService);
    }

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
    NetworkManager::get().onChatMessage = [chatService, &luauEngine](PeerId senderId, const std::string& text) {
        std::cout << "[Chat][Peer " << senderId << "] " << text << std::endl;
        if (chatService) {
            chatService->receiveMessage(senderId, text);
            luauEngine->fireChatMessage(chatService.get(), senderId, text);
        }
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
    // 先にスクリプトを実行開始する
    luauEngine->executeWorkspaceScripts(*workspace);
    luauEngine->executeSystemScripts();

    // その後にキャラクターをスポーンする
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

        // ---- ネットワークポーリング（物理更新より前＝受信内容を反映してからシミュレートする） ----
        NetworkManager::get().update(deltaTime);
        // ナビメッシュ生成中も完了通知と接続維持は進めるが、世界シミュレーションは止める。
        luauEngine->pollPathfindingRequests();
        bool navMeshBusy = PathfindingService::IsBuildActive();
        const PeerId authoritativeId = NetworkManager::get().isActive()
            ? NetworkManager::get().getLocalPeerId() : 0;
        if (authoritativeId != 0 && user->peerId != authoritativeId) {
            if (!user->applyNetworkIdentity(authoritativeId)) {
                RCBN_ERROR("Canonical identity rename failed after authority change");
                break;
            }
        }
        if (consoleChat) {
            std::deque<std::string> pending;
            {
                std::lock_guard<std::mutex> lock(consoleChat->mutex);
                pending.swap(consoleChat->lines);
            }
            for (const auto& line : pending) {
                if (NetworkManager::get().getLocalPeerId() == 0) {
                    std::cout << "[Chat] Not connected yet; message was not sent." << std::endl;
                } else {
                    if (chatService) chatService->sendMessage(line);
                }
            }
        }

        // レプリケーション(受信姿勢の適用と自姿勢の送信)。物理更新より前に行う
        if (!navMeshBusy)
            replication.update(deltaTime, workspace->getPhysicsEngine());
        if (replication.hasFatalIdentityError()) break;

        if (!navMeshBusy) {
            FrameProfiler::get().beginSection("physics");
            if (workspace->getPhysicsEngine()) workspace->getPhysicsEngine()->update(*workspace, deltaTime);
            FrameProfiler::get().endSection("physics");
            FrameProfiler::get().beginSection("luau");
            luauEngine->resetFrameSafetyCounters();
            luauEngine->fireHeartbeat(deltaTime);
            navMeshBusy = PathfindingService::IsBuildActive();
            if (!navMeshBusy) {
                luauEngine->update(deltaTime);
                navMeshBusy = PathfindingService::IsBuildActive();
            }
            if (!navMeshBusy) {
                luauEngine->executeWorkspaceScripts(*workspace);
                navMeshBusy = PathfindingService::IsBuildActive();
            }
            if (!navMeshBusy) {
                luauEngine->executeSystemScripts();
                navMeshBusy = PathfindingService::IsBuildActive();
            }
            FrameProfiler::get().endSection("luau");
        }
        if (luauEngine->consumeSafetyHaltRequest()) break; // 既存のconsumeExitRequestと同じglfwTerminate()クリーンアップ経路に合流
        navMeshBusy = PathfindingService::IsBuildActive();

        // エディタが存在しないため、常にゲームプレイ入力として扱う
        const bool debugInput = runtimeEditorPtr && runtimeEditorPtr->isDebugCapturingKeyboard();
        const bool chatInput = renderer->isChatCapturingKeyboard() || ImGui::GetIO().WantTextInput;
        const bool uiInput = chatInput || debugInput;
        SystemState::get().viewportFocused = !uiInput;
        if (!navMeshBusy) {
            user->processInput(workspace->getPhysicsEngine(), deltaTime,
                               /*viewportFocused=*/!uiInput, /*viewportZoomEnabled=*/!uiInput,
                               /*isGameplayInput=*/!uiInput,
                               uiInput);
        }
        if (user->consumeExitRequest()) break;

        // ---- Pキー: Workspace切り替え ----
        if (!navMeshBusy && user->consumeWorkspaceSwitchRequest()) {
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
        if (!navMeshBusy)
            Humanoid::updateAll(workspace.get(), deltaTime, workspace->getPhysicsEngine());

        // Humanoidのパーツ配置(processInput内のapplyBodyAnimation)が終わった直後に、
        // アンカー駆動のキネマティックWeld(帽子等)を即時同期して追従ラグを無くす
        if (!navMeshBusy && workspace->getPhysicsEngine())
            workspace->getPhysicsEngine()->syncWeldKinematics();

        if (!navMeshBusy) {
            Weather::updateAll(workspace.get(), deltaTime, user->cpos);
            ParticleEmitter::updateAll(workspace.get(), deltaTime);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace);

        audioService->updateSounds(user->cpos, user->right);
        FrameProfiler::get().endFrame();
    }

    // ---- クリーンアップ ----
    NetworkManager::get().shutdown();
    NetworkManager::get().onRoleChanged = nullptr;
    NetworkManager::get().onGameMessage = nullptr;
    NetworkManager::get().onChatMessage = nullptr;
    if (chatService) chatService->onSendRequested = nullptr;
    luauEngine->cancelAllTasks();
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
