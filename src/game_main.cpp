#include <GL/glew.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
    #include <windows26.h>
#endif

#ifdef __APPLE__
    #include <mach-o/dyld.h>
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
#include <Util/RuntimeLaunchArgs.hpp>
#include <Util/RuntimeFileSystem.hpp>
#include <Util/SystemExtensionPermissions.hpp>
#include <Util/UUID.hpp>
#include <Util/YamlLoadResult.hpp>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <yaml-cpp/yaml.h>
#include "include/stb_image.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <filesystem>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// ===================================================
//  startup.yaml からゲーム設定を読み込む
// ===================================================
struct GameConfig {
    std::string gameName  = "Recubin Game";
    std::string startScene = "assets/scenes/game.yaml";
    bool debugLog = false; // ランタイムのコンソールを表示するかどうか（未実装）
    std::string stunServer;
    std::string rendezvousServer;
    // todo: debugLogの書き込み処理を追加しておく
};

// ===================================================
//  --host [listen-port] でルーム作成、--connect <room-code> で参加する。
//  --direct-host <port> / --direct-connect <host:port> で直接接続する。
//  引数なしなら従来通りNetworkRole::Offlineのまま動作する。
// ===================================================
static constexpr uint16_t kDefaultStunPort = 3478;
static constexpr uint16_t kDefaultRendezvousPort = 3479;

struct NetworkLaunchArgs {
    bool asHost = false;
    bool asClient = false;
    bool asDirectHost = false;
    bool asDirectClient = false;
    bool valid = true;
    std::string roomCode;
    std::string directHost;
    uint16_t directPort = 0;
    uint16_t listenPort = 0;
    std::string stunServer;
    std::string rendezvousServer;
};

struct ConsoleChatQueue {
    std::mutex mutex;
    std::deque<std::string> lines;
};

static bool parsePort(std::string_view value, bool allowZero, uint16_t& port) {
    if (value.empty()) return false;
    uint32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed > 65535 || (!allowZero && parsed == 0)) {
        return false;
    }
    port = static_cast<uint16_t>(parsed);
    return true;
}

static bool isValidDirectHost(const std::string& host) {
    if (host.empty() || host.size() > 253 ||
        host.front() == '.' || host.back() == '.') {
        return false;
    }
    size_t labelStart = 0;
    while (labelStart < host.size()) {
        const size_t labelEnd = host.find('.', labelStart);
        const size_t end = labelEnd == std::string::npos ? host.size() : labelEnd;
        if (end == labelStart || end - labelStart > 63 ||
            host[labelStart] == '-' || host[end - 1] == '-') {
            return false;
        }
        for (size_t index = labelStart; index < end; ++index) {
            const unsigned char c = static_cast<unsigned char>(host[index]);
            if (!std::isalnum(c) && c != '-') return false;
        }
        if (labelEnd == std::string::npos) break;
        labelStart = labelEnd + 1;
    }

    bool allNumericOrDots = true;
    for (const unsigned char c : host) {
        if (!std::isdigit(c) && c != '.') {
            allNumericOrDots = false;
            break;
        }
    }
    if (!allNumericOrDots || host.find('.') == std::string::npos) return true;

    size_t start = 0;
    int octets = 0;
    while (start < host.size()) {
        const size_t dot = host.find('.', start);
        const size_t end = dot == std::string::npos ? host.size() : dot;
        uint32_t octet = 0;
        const std::string_view value(host.data() + start, end - start);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), octet);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
            octet > 255 || ++octets > 4) {
            return false;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return octets == 4;
}

static bool parseDirectEndpoint(const std::string& value,
                                std::string& host,
                                uint16_t& port) {
    const size_t colon = value.find(':');
    if (colon == std::string::npos || colon != value.rfind(':') ||
        colon == 0 || colon + 1 >= value.size()) {
        return false;
    }
    host = value.substr(0, colon);
    return isValidDirectHost(host) &&
           parsePort(std::string_view(value).substr(colon + 1), false, port);
}

static NetworkLaunchArgs parseNetworkArgs(int argc, char* argv[]) {
    NetworkLaunchArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host") {
            args.asHost = true;
            if (i + 1 < argc) {
                std::string maybePort = argv[i + 1];
                if (!maybePort.empty() && maybePort[0] != '-') {
                    if (!parsePort(maybePort, true, args.listenPort)) args.valid = false;
                    ++i;
                }
            }
        } else if (arg == "--connect") {
            args.asClient = true;
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                args.valid = false;
            } else {
                args.roomCode = argv[++i];
            }
        } else if (arg == "--direct-host") {
            args.asDirectHost = true;
            if (i + 1 >= argc ||
                !parsePort(argv[++i], false, args.directPort)) {
                args.valid = false;
            }
        } else if (arg == "--direct-connect") {
            args.asDirectClient = true;
            if (i + 1 >= argc ||
                !parseDirectEndpoint(argv[++i], args.directHost, args.directPort)) {
                args.valid = false;
            }
        } else if (arg == "--listen-port") {
            if (i + 1 >= argc ||
                !parsePort(argv[++i], true, args.listenPort)) {
                args.valid = false;
            }
        } else if (arg == "--stun") {
            if (i + 1 < argc) { args.stunServer = argv[i + 1]; ++i; }
        } else if (arg == "--rendezvous") {
            if (i + 1 < argc) { args.rendezvousServer = argv[i + 1]; ++i; }
        }
    }
    return args;
}

static bool parseServerAddress(const std::string& value,
                               uint16_t defaultPort,
                               std::string& host,
                               uint16_t& port) {
    if (value.empty()) return false;
    const size_t colon = value.rfind(':');
    if (colon == std::string::npos) {
        host = value;
        port = defaultPort;
        return true;
    }
    if (colon == 0 || colon + 1 >= value.size()) return false;
    host = value.substr(0, colon);
    try {
        const int parsed = std::stoi(value.substr(colon + 1));
        if (parsed <= 0 || parsed > 65535) return false;
        port = static_cast<uint16_t>(parsed);
    } catch (...) {
        return false;
    }
    return true;
}

static GameConfig loadStartup() {
    GameConfig cfg;
    const YamlLoadResult loaded = loadYamlFile("startup.yaml");
    if (!loaded.success) {
        // startup.yaml is optional, but a present and malformed file must not
        // fail silently and fall back to defaults.
        if (std::filesystem::exists("startup.yaml")) {
            RCBN_ERROR("Failed to load startup.yaml: " << loaded.error);
        }
        return cfg;
    }
    try {
        YAML::Node node = loaded.node;
        if (node["GameName"])   cfg.gameName   = node["GameName"].as<std::string>();
        if (node["StartScene"]) cfg.startScene = node["StartScene"].as<std::string>();
        if (node["DebugLog"])   cfg.debugLog   = node["DebugLog"].as<bool>();
        if (node["StunServer"]) cfg.stunServer = node["StunServer"].as<std::string>();
        if (node["RendezvousServer"]) cfg.rendezvousServer = node["RendezvousServer"].as<std::string>();
    } catch (const std::exception& error) {
        RCBN_ERROR("Invalid startup.yaml values: " << error.what());
    } catch (...) {
        RCBN_ERROR("Invalid startup.yaml values: unknown error");
    }
    return cfg;
}

#ifdef __APPLE__
// Finder launches an app with an unspecified working directory.  Resolve the
// bundle's Resources directory from the executable location before reading
// startup.yaml so all existing relative asset paths continue to work.
static void prepareMacBundleResources() {
    uint32_t bufferSize = 0;
    _NSGetExecutablePath(nullptr, &bufferSize);
    if (bufferSize == 0) return;

    std::vector<char> buffer(bufferSize + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &bufferSize) != 0) return;

    std::error_code ec;
    const std::filesystem::path executable =
        std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), ec);
    if (ec) return;

    const std::filesystem::path resources = executable.parent_path().parent_path() / "Resources";
    if (!std::filesystem::exists(resources / "startup.yaml")) return;
    std::filesystem::current_path(resources, ec);
}
#endif



// ===================================================
//  main
// ===================================================
int main(int argc, char* argv[]) {
    // コンソールの出力/入力コードページをUTF-8にする
    // (Windows日本語版等では既定のANSIコードページのままだと、UTF-8で書かれた
    //  ログやLuauのprint出力が文字化けする)
    getPlatform().setupConsoleUtf8();
    getPlatform().setupDllSearchPath();
    if (!Physics::configureBackendFromCommandLine(argc, argv)) return -1;

    const RuntimeLaunchArgs runtimeArgs = parseRuntimeLaunchArgs(argc, argv);
    if (!runtimeArgs.valid) {
        RCBN_ERROR("Invalid runtime arguments: " << runtimeArgs.error);
        return -1;
    }

    GameConfig cfg = loadStartup();
    if (runtimeArgs.scenePath.has_value()) {
        std::error_code sceneError;
        if (!std::filesystem::is_regular_file(*runtimeArgs.scenePath, sceneError)) {
            RCBN_ERROR("Invalid --scene path: " << *runtimeArgs.scenePath);
            return -1;
        }
        cfg.startScene = *runtimeArgs.scenePath;
    }
    if (runtimeArgs.windowTitle.has_value()) cfg.gameName = *runtimeArgs.windowTitle;

    // ---- ルームコード/STUNネットワーク起動引数 ----
    NetworkLaunchArgs netArgs = parseNetworkArgs(argc, argv);
    const int networkModeCount =
        static_cast<int>(netArgs.asHost) + static_cast<int>(netArgs.asClient) +
        static_cast<int>(netArgs.asDirectHost) + static_cast<int>(netArgs.asDirectClient);
    if (!netArgs.valid || networkModeCount > 1) {
        RCBN_ERROR("Invalid network arguments: choose one of --host, --connect, "
                   "--direct-host, or --direct-connect and use valid ports");
        return -1;
    }
    const bool networkRequested = networkModeCount == 1;
    if (runtimeArgs.editorTest &&
        (!runtimeArgs.scenePath.has_value() || !runtimeArgs.windowTitle.has_value() ||
         !netArgs.asDirectClient || netArgs.directHost != "127.0.0.1")) {
        RCBN_ERROR("Invalid --editor-test usage: requires --scene, --window-title, "
                   "and --direct-connect 127.0.0.1:<port>");
        return -1;
    }
    // System.UseNetwork is loaded from the scene, so networking starts after loadAndBind.

    // 標準入力はblockingなので、レンダー/ネットワークスレッドから分離する。
    // readerはゲーム本体を参照せずshared queueだけを所有するため、getline待ちのまま
    // ウィンドウを閉じてもjoinで終了を妨げない。
    std::shared_ptr<ConsoleChatQueue> consoleChat;
    if (networkRequested) {
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

    // 通常ランタイムはゲームフォルダ(cwd)外のアセット読み込みを禁止する。
    // localhost専用のEditorテストクライアントだけは、未パッケージの絶対アセットを
    // Editorと同様に参照できるようAssetGuardを有効化しない。
    // AssetGuard is configured after Scene bind, once System's external-file
    // permission is known.

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
    glfwSwapInterval(1);
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
    if (bound.loadStatus != SceneLoader::LoadStatus::Success &&
        !(cfg.startScene.empty() && bound.loadStatus == SceneLoader::LoadStatus::NotFound)) {
        std::cerr << "[Runtime] Scene load failed: " << bound.loadMessage << std::endl;
        return -1;
    }
    auto workspaces = bound.workspaces;
    auto workspace  = bound.workspace;

    if (!RecubinUUID::isValid(system->ApplicationId)) {
        RCBN_ERROR("Scene has no ApplicationId; refusing to start runtime file system.");
        return -1;
    }
    auto runtimeFs = std::make_shared<RuntimeFileSystem>(
        system->ApplicationId,
        runtimeArgs.editorTest ? RuntimeFileSystem::Namespace::Editor
                                : RuntimeFileSystem::Namespace::Runtime,
        system->EnableExternalFileAccess);
    luauEngine->setRuntimeFileSystem(runtimeFs);
    if (runtimeArgs.editorTest) {
        std::cout << "[EditorTest] AssetGuard disabled for localhost editor test client."
                  << std::endl;
    } else if (!system->EnableExternalFileAccess) {
        AssetGuard::enableSandbox(std::filesystem::current_path());
    }

    // Consent is deliberately checked after binding the scene (so the live
    // System flags are authoritative), but before network/physics/scripts.
    if (!runtimeArgs.editorTest) {
        SystemExtensionPermissions permissions{
            system->EnableIOAPI, system->EnableIPCAPI, system->EnableExternalFileAccess};
        const auto applicationRoot = runtimeFs->namespaceRoot().parent_path();
        const bool anyEnabled = permissions.io || permissions.ipc || permissions.external;
        if (!anyEnabled) {
            if (!SystemExtensionConsent::write(applicationRoot, permissions))
                RCBN_WARN("System extension receipt could not be written; it will be retried.");
        } else if (SystemExtensionConsent::shouldWarn(applicationRoot, permissions)) {
            bool continueRuntime = false;
            bool quitRuntime = false;
            while (!continueRuntime && !quitRuntime && !glfwWindowShouldClose(window)) {
                glfwPollEvents();
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(.5f, .5f));
                ImGui::SetNextWindowSize(ImVec2(520.f, 0.f), ImGuiCond_Always);
                ImGui::Begin("System extensions###SystemExtensionConsent", nullptr,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::TextWrapped("This game requests the following system extensions:");
                if (permissions.io) ImGui::BulletText("I/O API - read and write game data files.");
                if (permissions.ipc) ImGui::BulletText("IPC API - communicate with external processes.");
                if (permissions.external) ImGui::BulletText("External File Access - access files outside the game data directory.");
                ImGui::Spacing();
                ImGui::TextWrapped("These permissions may read, modify, or communicate outside the game sandbox.");
                if (ImGui::Button("Continue", ImVec2(120, 0))) continueRuntime = true;
                ImGui::SameLine();
                if (ImGui::Button("Quit", ImVec2(120, 0))) quitRuntime = true;
                ImGui::End();
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(window);
            }
            if (!continueRuntime) {
                glfwDestroyWindow(window);
                glfwTerminate();
                return 0;
            }
            if (!SystemExtensionConsent::write(applicationRoot, permissions))
                RCBN_WARN("System extension receipt could not be written; it will be shown next launch.");
        }
    }

    // Networked games do not start scripts, character spawning, physics, or replication
    // until the host-authoritative PeerId is known.
    if (system->UseNetwork && networkRequested) {
        const bool directMode = netArgs.asDirectHost || netArgs.asDirectClient;
        bool configOk = true;
        bool started = false;
        if (directMode) {
            started = netArgs.asDirectHost
                ? NetworkManager::get().startHost(netArgs.directPort)
                : NetworkManager::get().connect(
                    netArgs.directHost, netArgs.directPort, netArgs.listenPort);
        } else {
            NatConfig natConfig;
            natConfig.listenPort = netArgs.listenPort;
            const std::string stun =
                netArgs.stunServer.empty() ? cfg.stunServer : netArgs.stunServer;
            const std::string rendezvous =
                netArgs.rendezvousServer.empty() ? cfg.rendezvousServer : netArgs.rendezvousServer;
            configOk =
                parseServerAddress(stun, kDefaultStunPort, natConfig.stunHost, natConfig.stunPort) &&
                parseServerAddress(rendezvous, kDefaultRendezvousPort,
                                   natConfig.rendezvousHost, natConfig.rendezvousPort);
            if (configOk) {
                started = netArgs.asHost
                    ? NetworkManager::get().createRoom(natConfig)
                    : NetworkManager::get().joinRoom(netArgs.roomCode, natConfig);
            } else {
                RCBN_ERROR("NetworkManager: MissingConfig: configure StunServer and RendezvousServer");
            }
        }
        if (!started) {
            const ConnectionError error = configOk
                ? NetworkManager::get().getConnectionError()
                : ConnectionError::MissingConfig;
            glfwTerminate();
            return -(10 + static_cast<int>(error));
        }

        double lastTick = glfwGetTime();
        while (!glfwWindowShouldClose(window)) {
            const double now = glfwGetTime();
            NetworkManager::get().update(static_cast<float>(now - lastTick));
            lastTick = now;
            glfwPollEvents();
            const ConnectionState state = NetworkManager::get().getConnectionState();
            if (state == ConnectionState::Connected || state == ConnectionState::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (NetworkManager::get().getConnectionState() == ConnectionState::Failed) {
            const ConnectionError error = NetworkManager::get().getConnectionError();
            RCBN_ERROR("Network startup failed: "
                       << NetworkManager::connectionErrorToString(error));
            NetworkManager::get().shutdown();
            glfwTerminate();
            return -(10 + static_cast<int>(error));
        }
        if (glfwWindowShouldClose(window)) {
            NetworkManager::get().shutdown();
            glfwTerminate();
            return 0;
        }
        if (netArgs.asHost) {
            std::cout << "[Network] Room code: "
                      << NetworkManager::get().getRoomCode() << std::endl;
        }
    }

    if (system->UseNetwork && networkRequested) {
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
    // Chat is a network feature.  ChatService still exists for Luau/API
    // compatibility, but the runtime overlay must not be installed when the
    // scene explicitly disables networking.
    if (chatService && system->UseNetwork) {
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
    user->spawnCharacter(system.get(), workspace.get());
    audioService->playAutoPlaySounds();
    if (user->character) workspace->addChild(user->character);

    // ランタイムは常にプレイ状態。エディターと違いフラグを設定する箇所が無いため明示する
    // （ProximityPrompt 表示・キー判定・GUIボタンのクリック・物理の enqueue 経路に必要）
    SystemState::get().isPlaying       = true;
    SystemState::get().viewportFocused = true;
    SystemState::get().inputState      = InputState::Gameplay;

    double lastFrame = glfwGetTime();
    int networkExitCode = 0;

    // ---- メインループ（常にプレイ状態） ----
    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float deltaTime = static_cast<float>(std::max(0.0, now - lastFrame));
        lastFrame       = now;
        SystemState::get().deltaTime = deltaTime;

        // ---- ネットワークポーリング（物理更新より前＝受信内容を反映してからシミュレートする） ----
        NetworkManager::get().update(deltaTime);
        if (NetworkManager::get().getConnectionState() == ConnectionState::Failed) {
            const ConnectionError error = NetworkManager::get().getConnectionError();
            RCBN_ERROR("Network connection failed: "
                       << NetworkManager::connectionErrorToString(error));
            networkExitCode = -(10 + static_cast<int>(error));
            break;
        }
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
                               /*viewportFocused=*/!uiInput, /*viewportHovered=*/!uiInput,
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
                    replication.setWorkspace(workspace);
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
            Vector3 terrainCenter = user->cpos;
            if (user->humanoid) {
                if (auto root = user->humanoid->getRootPart())
                    terrainCenter = root->getWorldCFrame().Position;
            }
            SceneRuntime::updateTerrains(workspace.get(), terrainCenter);
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
    SceneRuntime::releaseTerrainStreamers(workspaces);
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
    return networkExitCode;
}
