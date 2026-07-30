#ifdef _WIN32
    #include <windows26.h>
    #define windows(...) __VA_ARGS__ // Use for windows
#else
    #define windows(...)
#endif

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
#include <Instances/Decal.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Weather.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/PathfindingService.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/SceneRuntime.hpp>
#include <Core/FileLoader.hpp>
#include <Core/AudioService.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>

#include <Editor/EditorManager.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/Localization.hpp>
#include <Core/SystemState.hpp>
#include <include/imgui/imgui.h>

#include <Util/Logger.hpp>
#include <Util/FrameProfiler.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>

#include <iostream>
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstddef>
#include <yaml-cpp/yaml.h>
#include "include/stb_image.h"

#include <PhysX/PxPhysicsAPI.h>
#include <memory>

#define ENABLE_BETA 1 // enable beta features
#if ENABLE_BETA
    #define beta(...) __VA_ARGS__
#else
    #define beta(...)
#endif

// ===================================================
//  ウィンドウセットアップ
// ===================================================
GLFWwindow* setupWindow() {
    std::cout << "initing GLFW...\n";

#ifdef _WIN32
    // GLFW/ImGuiがWindowsの論理座標とFramebuffer座標を正しく扱えるよう、
    // ウィンドウ生成より前にPer-Monitor V2 DPI awarenessを有効にする。
    // 古いWindows環境で利用できない場合は従来のシステムDPI対応へフォールバックする。
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
#endif

    if (!glfwInit()) {
        std::cout << "GLFW init failed\n";
        return nullptr;
    }

    // Mac対応: OpenGL 4.1 Core Profileを明示指定（macOSは未指定だとレガシー2.1しか得られない）
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

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
    glewExperimental = GL_TRUE; // Core Profileで必要
    if (glewInit() != GLEW_OK) {
        std::cout << "GLEW init failed\n";
        return nullptr;
    }
    return window;
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

// シーンの再読込前に System 配下を全てリセットする（Users コンテナと、その中の user のみ例外）。
// Workspace 以外にも PathfindingService/StarterCharacter 等が System 直下に
// 残り続けると、再読込で同名の新規インスタンスが追加された際にInstance::setParent
// のキー衝突（残存コピー・警告連発の原因）を招くため、Users(user)以外は無条件で除去する。
// user 自身は System/Users 配下で使い回すが、その子（Inventory等）もシーンYAMLから毎回
// 作り直されるため、同じ理由で user の子も合わせてクリアする（残すとInventoryが同様に
// キー衝突→増殖する）。Users コンテナ自体も user 同様使い回す（毎回作り直すと
// user->Parent の張替えが不要に発生するため）。
static void resetSystemForReload(
    const std::shared_ptr<System>& system,
    const std::shared_ptr<User>& user)
{
    if (!system) return;

    // System/UserはPlay/Stop・シーンLoadを跨いで同一インスタンスが使い回されるため、
    // ScriptがConnect()したコールバックをここで明示的に切断しないと、次回Play/Load時に
    // 古いコールバックが残ったまま発火し続けてしまう(LuaState自体もPlay/Stopで
    // 再生成されないため、disconnectしない限りLuaレジストリ参照が永遠に残る)。
    // 呼び出し元(Stop遷移・Loadボタン)の両方がこの関数を経由するため、ここに集約する。
    if (system->Heartbeat) system->Heartbeat->disconnectAll();
    if (user) {
        if (user->CharacterAdded) user->CharacterAdded->disconnectAll();
        if (user->Input) {
            if (user->Input->Pressed)  user->Input->Pressed->disconnectAll();
            if (user->Input->Released) user->Input->Released->disconnectAll();
        }
    }

    std::shared_ptr<Instance> usersContainer;
    for (auto& [name, child] : system->getChildren()) {
        if (child && child->IsA("Users")) { usersContainer = child; break; }
    }

    std::vector<std::string> namesToRemove;
    for (auto& [name, child] : system->getChildren()) {
        if (child.get() == usersContainer.get()) continue;
        namesToRemove.push_back(name);
    }
    for (auto& name : namesToRemove) {
        system->removeChild(name);
    }

    if (usersContainer) {
        std::vector<std::string> usersChildNames;
        for (auto& [name, child] : usersContainer->getChildren()) {
            if (child.get() == user.get()) continue;
            usersChildNames.push_back(name);
        }
        for (auto& name : usersChildNames) {
            usersContainer->removeChild(name);
        }
    }

    if (user) {
        std::vector<std::string> userChildNames;
        for (auto& [name, child] : user->getChildren()) {
            userChildNames.push_back(name);
        }
        for (auto& name : userChildNames) {
            user->removeChild(name);
        }
        // Inventory本体も再利用せず、前回の子とSlotsの強参照をまとめて破棄する。
        user->resetInventory();
    }
}

// シーン破棄の前に Terrain の streamer を明示的に解放する。
// Workspace のメンバ(m_ownedPhysics)は基底の children(Terrain) より先に破棄されるため、
// Workspace 破棄に任せると ~TerrainStreamer が解放済み Physics を参照してクラッシュする。
// ここで物理が生きているうちに reset し、ワーカー停止・GL/PhysX 解放・"terrain"フラッシュを完了させる。
static void resetTerrainStreamers(const std::vector<std::shared_ptr<Workspace>>& workspaces)
{
    for (auto& ws : workspaces) {
        if (!ws) continue;
        for (auto& [name, child] : ws->getChildren()) {
            if (child->IsA("Terrain")) {
                static_cast<Terrain*>(child.get())->streamer.reset();
            }
        }
    }
}


// エディター設定（前回開いていたシーンパスなど）の保存先
static const std::string kEditorSettingsPath = "editor_settings.yaml";

// editor_settings.yaml 全体を読み込む（無ければ空ノード）
static YAML::Node loadEditorSettings() {
    try {
        return YAML::LoadFile(kEditorSettingsPath);
    } catch (...) {
        return YAML::Node();
    }
}

// editor_settings.yaml 全体を書き出す（既存キーを保持してマージ保存するために使う）
static void writeEditorSettings(const YAML::Node& root) {
    YAML::Emitter out;
    out << root;
    std::ofstream file(kEditorSettingsPath);
    if (file) file << out.c_str();
}

// 前回開いていたシーンパスを読み込む。記録が無い/壊れている場合は空文字列を返す
static std::string loadLastScenePath() {
    YAML::Node root = loadEditorSettings();
    if (root && root["LastScenePath"]) return root["LastScenePath"].as<std::string>();
    return "";
}

// 次回起動時に自動で開けるよう、現在のシーンパスを記録する（他キーは保持）
static void saveLastScenePath(const std::string& path) {
    YAML::Node root = loadEditorSettings();
    root["LastScenePath"] = path;
    writeEditorSettings(root);
}

// 各パネルの開閉状態を editor_settings.yaml から復元する。
// 記録が無い場合は既定（Animation のみ非表示、他は表示）を使う。
static void loadPanelVisibility(EditorManager* ed) {
    if (!ed) return;
    YAML::Node p = loadEditorSettings()["Panels"];
    auto get = [&](const char* key, bool def) -> bool {
        return (p && p[key]) ? p[key].as<bool>() : def;
    };
    ed->hierarchyPanel->isOpen      = get("Explorer",       true);
    ed->propertiesPanel->isOpen     = get("Properties",     true);
    ed->viewportPanel->isOpen       = get("Viewport",       true);
    ed->contentBrowserPanel->isOpen = get("ContentBrowser", true);
    ed->consolePanel->isOpen        = get("Console",        true);
    ed->animationPanel->isOpen      = get("Animation",      false);
}

// 各パネルの開閉状態を editor_settings.yaml へ保存する（他キーは保持）
static void savePanelVisibility(EditorManager* ed) {
    if (!ed) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;
    p["Explorer"]       = ed->hierarchyPanel->isOpen;
    p["Properties"]     = ed->propertiesPanel->isOpen;
    p["Viewport"]       = ed->viewportPanel->isOpen;
    p["ContentBrowser"] = ed->contentBrowserPanel->isOpen;
    p["Console"]        = ed->consolePanel->isOpen;
    p["Animation"]      = ed->animationPanel->isOpen;
    root["Panels"] = p;
    writeEditorSettings(root);
}

// エディター環境設定（物理デバッグ表示・言語・カメラ・スナップ/フィット・ギズモモード）を
// editor_settings.yaml から復元する。記録が無い/壊れている場合は既定値のまま変更しない。
static void loadEditorPreferences(EditorManager* ed, User* user) {
    if (!ed || !user) return;
    YAML::Node p = loadEditorSettings()["Preferences"];
    if (!p) return;

    if (p["PhysicsDebug"]) ed->viewportPanel->showPhysicsDebug = p["PhysicsDebug"].as<bool>();

    if (p["Language"]) {
        std::string lang = p["Language"].as<std::string>();
        if (lang == "JA") Loc::setLanguage(Loc::Lang::JA);
        else if (lang == "EN") Loc::setLanguage(Loc::Lang::EN);
    }

    if (p["Camera"]) {
        YAML::Node c = p["Camera"];
        if (c["Pos"] && c["Pos"].IsSequence() && c["Pos"].size() == 3 &&
            c["Rot"] && c["Rot"].IsSequence() && c["Rot"].size() == 4) {
            Vector3 pos(c["Pos"][0].as<float>(), c["Pos"][1].as<float>(), c["Pos"][2].as<float>());
            Quaternion rot(
                c["Rot"][3].as<float>(), // w
                c["Rot"][0].as<float>(), // x
                c["Rot"][1].as<float>(), // y
                c["Rot"][2].as<float>()  // z
            );
            user->setCameraCFrame(CFrame(pos, rot));
        }
    }

    if (p["SnapTranslate"])    ed->viewportPanel->snapTranslate    = p["SnapTranslate"].as<bool>();
    if (p["SnapTranslateVal"]) ed->viewportPanel->snapTranslateVal = p["SnapTranslateVal"].as<float>();
    if (p["SnapRotate"])       ed->viewportPanel->snapRotate       = p["SnapRotate"].as<bool>();
    if (p["SnapRotateVal"])    ed->viewportPanel->snapRotateVal    = p["SnapRotateVal"].as<float>();
    if (p["SnapScale"])        ed->viewportPanel->snapScale        = p["SnapScale"].as<bool>();
    if (p["SnapScaleVal"])     ed->viewportPanel->snapScaleVal     = p["SnapScaleVal"].as<float>();
    if (p["CollisionFit"])     ed->viewportPanel->collisionFit     = p["CollisionFit"].as<bool>();

    if (p["GizmoOp"]) {
        int op = p["GizmoOp"].as<int>();
        if (op == ImGuizmo::TRANSLATE || op == ImGuizmo::ROTATE || op == ImGuizmo::SCALE) {
            ed->viewportPanel->gizmoOp = static_cast<ImGuizmo::OPERATION>(op);
        }
    }
    if (p["GizmoMode"]) {
        int mode = p["GizmoMode"].as<int>();
        if (mode == ImGuizmo::WORLD || mode == ImGuizmo::LOCAL) {
            ed->viewportPanel->gizmoMode = static_cast<ImGuizmo::MODE>(mode);
        }
    }
    if (p["SelectOnly"]) ed->viewportPanel->selectOnly = p["SelectOnly"].as<bool>();
    if (p["ToolNone"])   ed->viewportPanel->toolNone   = p["ToolNone"].as<bool>();
}

// エディター環境設定を editor_settings.yaml へ保存する（他キーは保持）
static void saveEditorPreferences(EditorManager* ed, User* user) {
    if (!ed || !user) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;

    p["PhysicsDebug"] = ed->viewportPanel->showPhysicsDebug;
    p["Language"] = (Loc::getLanguage() == Loc::Lang::JA) ? std::string("JA") : std::string("EN");

    CFrame camCf = user->getCameraCFrame();
    YAML::Node c;
    YAML::Node posNode;
    posNode.push_back(camCf.Position.x);
    posNode.push_back(camCf.Position.y);
    posNode.push_back(camCf.Position.z);
    c["Pos"] = posNode;
    YAML::Node rotNode;
    rotNode.push_back(camCf.Rotation.x);
    rotNode.push_back(camCf.Rotation.y);
    rotNode.push_back(camCf.Rotation.z);
    rotNode.push_back(camCf.Rotation.w);
    c["Rot"] = rotNode;
    p["Camera"] = c;

    p["SnapTranslate"]    = ed->viewportPanel->snapTranslate;
    p["SnapTranslateVal"] = ed->viewportPanel->snapTranslateVal;
    p["SnapRotate"]       = ed->viewportPanel->snapRotate;
    p["SnapRotateVal"]    = ed->viewportPanel->snapRotateVal;
    p["SnapScale"]        = ed->viewportPanel->snapScale;
    p["SnapScaleVal"]     = ed->viewportPanel->snapScaleVal;
    p["CollisionFit"]     = ed->viewportPanel->collisionFit;

    p["GizmoOp"]     = static_cast<int>(ed->viewportPanel->gizmoOp);
    p["GizmoMode"]   = static_cast<int>(ed->viewportPanel->gizmoMode);
    p["SelectOnly"]  = ed->viewportPanel->selectOnly;
    p["ToolNone"]    = ed->viewportPanel->toolNone;

    root["Preferences"] = p;
    writeEditorSettings(root);
}

// ウィンドウタイトルに現在開いているシーンのファイル名を付加する
static void updateWindowTitle(GLFWwindow* window, const std::string& scenePath, bool dirty = false) {
    std::string fileName = std::filesystem::path(scenePath).filename().string();
    std::string title = fileName.empty() ? "Recubin Studio" : ("Recubin Studio - " + fileName);
    if (dirty) title = "*" + title;  // 未保存マーク
    glfwSetWindowTitle(window, title.c_str());
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
//  --gen-test-scene: ヘッドレスシーン生成
//  GLFW/Renderer/PhysXを一切構築せず、SceneLoader::createInstance()が対応する
//  全クラスをWorkspace配下に生成してYAMLへ保存する。RecubinTest.exeでの
//  網羅的なリグレッション確認用（run_regression.pyから呼ばれる想定）。
// ===================================================
static int runGenTestScene(const std::string& outputPath) {
    getPlatform().setupConsoleUtf8();

    auto audioService = std::make_unique<AudioService>();
    audioService->initialize();

    auto system    = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    system->addChild(workspace);

    // SceneLoader::createInstance() が対応する全クラス（System/Workspace/
    // PathfindingService/Script は特殊なため除外。Script は末尾で検証用に個別追加する）。
    // createInstance() に新しいクラスを追加した場合はここにも追加すること。
    static const char* kClassNames[] = {
        "Cube", "Cylinder", "TriangularPrism", "Truss", "Seat", "Sphere", "MeshCube", "LiquidCube",
        "Skybox", "Sun", "Moon", "Model", "Sound",
        "Lighting", "PointLight", "SpotLight", "PostEffect",
        "AppImage", "FileRef", "Humanoid", "Animation", "StarterCharacter", "Terrain", "Instance",
        "Rope", "Rod", "Weld", "Motor", "Attachment", "Force",
        "TextLabel", "TextButton", "ImageLabel", "ImageButton", "SurfaceGui", "BillboardGui",
        "ProximityPrompt", "Folder", "Tool", "ParticleEmitter", "Weather",
    };

    int generated = 0;
    for (const char* className : kClassNames) {
        auto inst = SceneLoader::createInstance(className);
        if (!inst) {
            std::cout << "[gen-test-scene] WARNING: failed to create " << className << "\n";
            continue;
        }
        inst->Name = className;
        workspace->addChild(inst);
        ++generated;
    }

    // Decal/Texture はBaseCubeの子としてのみ意味を持つため、専用の親Cubeを作って生成する
    auto decalHost = SceneLoader::createInstance("Cube");
    decalHost->Name = "DecalHost";
    workspace->addChild(decalHost);
    for (const char* className : { "Decal", "Texture" }) {
        auto inst = SceneLoader::createInstance(className);
        if (!inst) continue;
        inst->Name = className;
        decalHost->addChild(inst);
    }

    // 検証用スクリプト: 代表的なインスタンスが揃っているかを確認する
    // (scripts/gen_test_scene_check.luau)。ContentPath参照にしているのは、
    // SceneLoader::saveNode がScriptのSourceを直接保存せずContentPathのみ
    // 書き出すため（インラインSourceは保存→再ロードで消えてしまう）。
    auto script = std::make_shared<Script>("scripts/gen_test_scene_check.luau");
    script->Name = "GenTestSceneCheck";
    workspace->addChild(script);

    SceneLoader::saveScene(system.get(), outputPath);
    std::cout << "[gen-test-scene] Wrote " << generated << " instances (+DecalHost, +check script) to "
              << outputPath << "\n";
    return 0;
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
    if (!Physics::configureBackendFromCommandLine(argc, argv)) return -1;

    std::cout << "Hello world!\n"
              << "Recubin Studio v0.9983\n";
    std::filesystem::path engineExePath = (argc > 0 && argv[0]) ? std::filesystem::path(argv[0]) : std::filesystem::path();

    windows(
        DWORD myPid = GetCurrentProcessId();
        std::string cmd = "Watcher.exe " + std::to_string(myPid);

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};

        // 外部監視プロセスを起動（クラッシュ時の再起動用）
        // ※ 子プロセスのライフサイクルは管理しないためハンドルは即破棄
        if (CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else {
            DWORD error = GetLastError();
            std::cerr << "Failed to start watcher.exe: " << error << '\n';
        }
    )

    // ヘッドレスシーン生成モード: GUI/GLFW/Rendererを一切構築せず即終了する
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gen-test-scene" && i + 1 < argc) {
            return runGenTestScene(argv[i + 1]);
        }
    }

    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    auto renderer     = std::make_unique<Renderer>();
    auto audioService = std::make_unique<AudioService>();
    auto system       = std::make_shared<System>();
    auto luauEngine   = std::make_unique<LuauEngine>();
    auto user         = std::make_shared<User>(std::make_unique<GLFWInputBackend>(window));
    user->controlMode = User::ControlMode::Free; // エディターではフリーモードから開始(パッケージされたゲームランタイムはCharacterモードから開始)
    // Inventory の初期化は SceneRuntime::loadAndBind() 側で行う
    // (シーンYAMLに保存済みのInventoryがあればそちらを優先採用するため、先読みで空Folderを付けない)

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

    // 起動時は無題の空シーンで開始し、ようこそタブから新規作成/前回の続き/読み込みを選ばせる
    // （旧: LastScenePath の自動ロード。前回パスは「前回の続きから」ボタンの源泉として温存する）
    std::string lastScenePath = loadLastScenePath();
    std::string scenePath; // 空 = 無題
    updateWindowTitle(window, scenePath);

    {
        auto bound = SceneRuntime::loadAndBind(scenePath, system, user, *luauEngine, window);
        workspace  = bound.workspace;
        workspaces = bound.workspaces;
    }
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
    renderer->m_onButtonActivated = [&](GuiButton* btn) {
        luauEngine->onGuiButtonActivated(btn);
    };

    // ===================================================
    //  EditorManager を Renderer に接続
    // ===================================================
    auto editorOwned = std::make_unique<EditorManager>(workspace.get(), user.get(), system.get());
    EditorManager* ed = editorOwned.get();
    ed->engineExePath = engineExePath.string();
    ed->scenePath     = scenePath; // 起動時に決定したシーンパスを反映
    loadPanelVisibility(ed);       // 前回のパネル開閉状態を復元
    loadEditorPreferences(ed, user.get()); // 前回のエディター環境設定を復元

    ed->welcomePanel->lastScenePath = lastScenePath;
    ed->welcomePanel->isOpen = true; // ようこそタブは起動時に必ず表示する

    // Workspace 切り替えコールバックを設定
    ed->hierarchyPanel->onSwitchWorkspace = [&](Workspace* ws) {
        auto wsSp = std::static_pointer_cast<Workspace>(ws->shared_from_this());
        workspaces = SceneRuntime::collectWorkspaces(system);
        workspace = wsSp;
        luauEngine->setGlobalInstance("workspace", workspace);
        luauEngine->setWorkspace(workspace);
        ed->setWorkspace(workspace.get());
        // Terrainを新Workspaceにリセット（次のupdate()で再初期化される）
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

    // The editor does not need a busy loop while it is idle.  Keep the
    // timeout short enough to remain responsive, while avoiding a full-rate
    // redraw when there is nothing to update.
    constexpr double editorIdleTimeoutSeconds = 2.0;
    constexpr double editorIdleFrameSeconds   = 1.0 / 15.0;
    double lastEditorInteraction = glfwGetTime();
    bool energySavingMode = false;
    std::vector<unsigned char> previousKeyState(GLFW_KEY_LAST + 1, GLFW_RELEASE);
    std::array<unsigned char, 3> previousMouseState{
        GLFW_RELEASE, GLFW_RELEASE, GLFW_RELEASE
    };
    double previousCursorX = 0.0;
    double previousCursorY = 0.0;
    int previousFramebufferWidth = 0;
    int previousFramebufferHeight = 0;
    glfwGetCursorPos(window, &previousCursorX, &previousCursorY);
    glfwGetFramebufferSize(window, &previousFramebufferWidth, &previousFramebufferHeight);

    auto initNewScene = [&](const std::string& path, bool isDirty) {
        auto bound = SceneRuntime::loadAndBind(path, system, user, *luauEngine, window);
        workspace  = bound.workspace;
        workspaces = bound.workspaces;
        ed->setWorkspace(workspace.get());
        if (isDirty) ed->markDirty();
        workspace->initPhysics();
    };

    while (true) {
        const bool windowInactive =
            glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_FALSE ||
            glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
        if (!wasPlaying && !PathfindingService::IsBuildActive() &&
            (energySavingMode || windowInactive)) {
            // This wakes on any GLFW event (input, resize, close, etc.) and
            // otherwise gives us one low-FPS refresh to keep the UI alive.
            glfwWaitEventsTimeout(editorIdleFrameSeconds);
        }

        if (glfwWindowShouldClose(window)) {
            if (checkExit(ed, *window)) {
                break;
            }
        }
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        // ワールド更新が停止中でも、非同期FindPathの完了だけは毎フレーム処理する。
        luauEngine->pollPathfindingRequests();
        bool navMeshBusy = PathfindingService::IsBuildActive();

        SystemState& state = SystemState::get();
        state.isPlaying  = ed && !ed->isEditMode();
        state.isPaused   = ed &&  ed->isPauseMode();
        state.inputState = state.isPlaying ? InputState::Gameplay : InputState::Editor;

        const bool isPlaying = state.isPlaying;
        const bool isPaused  = state.isPaused;

        // ---- Play/Stop 遷移処理 ----
        if (isPlaying && !wasPlaying) {
            // SlotsはInventoryから導出される実行時キャッシュ。EditorでToolを移動した
            // 直後でも前回のshared_ptrを持ち越さないよう、Play開始時に実体から再構築する。
            user->syncToolsFromInventory();
            // System配下(Workspace外)のスクリプトはPlay/Stopで破棄されないため、
            // 前回Playの実行状態(Completed等)をリセットして毎回最初から実行させる
            luauEngine->resetSystemScripts();
            snapshotDirty = ed && ed->isDirty();
            // Animation Editorの編集セッションが開いたままだと、リグ/プレビュー姿勢が
            // スナップショットに焼き込まれ、Stop後のシーンが静かに汚染される。保存前に必ず復元する
            if (ed && ed->animationPanel) ed->animationPanel->endEditSession();
            SceneLoader::saveScene(system.get(), snapshotPath);
            SceneLoader::resolveConstraintRefs(system.get());
            // 全WorkspaceのPhysicsを初期化
            for (auto& [name, child] : system->getChildren()) {
                if (child->IsA("Workspace")) {
                    auto* ws = static_cast<Workspace*>(child.get());
                    if (!ws->getPhysicsEngine()) ws->initPhysics();
                }
            }
            // 先にスクリプトを実行開始する
            for (auto& [name, child] : system->getChildren()) {
                if (child->IsA("Workspace")) {
                    auto* ws = static_cast<Workspace*>(child.get());
                    luauEngine->executeWorkspaceScripts(*ws);
                }
            }
            luauEngine->executeSystemScripts();

            // その後にキャラクターをスポーンする
            user->spawnCharacter(system.get());
            audioService->playAutoPlaySounds();
            if (user->character) workspace->addChild(user->character);
        }
        if (!isPlaying && wasPlaying) {
            audioService->stopAllSounds();
            user->despawnCharacter();
            // ---- Undo履歴/Clipboardのクリア（旧Workspace/Physics破棄より前に行う） ----
            // EditorManager の Undo スタックや Clipboard が BaseCube 等の shared_ptr を
            // 保持していると、resetSystemForReload() での実デストラクタ実行が
            // Workspace/Physics 破棄後まで遅延され、lastWorkspace がダングリングポインタ化
            // してクラッシュしうる（main.cpp末尾の明示的クリーンアップと同じ理由）。
            // Physics がまだ生きている今のうちにクリアする。
            if (ed) {
                ed->hierarchyPanel->selectedInstance = nullptr;
                ed->m_history.clear();
                ed->clearClipboard();
                ed->animationPanel->endEditSession();
            }
            // 全Workspaceのクリア（ownedPhysics デストラクタで自動解放）
            // Terrainは次回のload時に再構築される
            workspaces = SceneRuntime::collectWorkspaces(system);
            luauEngine->cancelAllTasks();
            resetTerrainStreamers(workspaces); // 物理が生きているうちにTerrainを解放
            clearWorkspacePhysics(workspaces);
            resetSystemForReload(system, user);

            initNewScene(snapshotPath, snapshotDirty);
        }
        wasPlaying = isPlaying;

        // ---- シーンのリロード（Loadボタン / 新規作成）----
        if (ed && (ed->pendingNewScene || !ed->pendingLoadPath.empty()) && ed->isEditMode()) {
            bool isNewScene = ed->pendingNewScene;
            std::string loadPath = isNewScene ? std::string() : ed->pendingLoadPath;
            ed->pendingNewScene = false;
            ed->pendingLoadPath.clear();

            // ---- Undo履歴/Clipboardのクリア（Play→Stop遷移と同じ理由） ----
            ed->hierarchyPanel->selectedInstance = nullptr;
            ed->m_history.clear();
            ed->clearClipboard();
            ed->animationPanel->endEditSession();

            // Terrainは次回のload時に再構築される
            workspaces = SceneRuntime::collectWorkspaces(system);
            luauEngine->cancelAllTasks();
            resetTerrainStreamers(workspaces); // 物理が生きているうちにTerrainを解放
            clearWorkspacePhysics(workspaces);
            resetSystemForReload(system, user);

            initNewScene(loadPath, false);
            ed->scenePath = loadPath;
            SceneRuntime::applyAppIcon(window, system.get());
        }

        // ---- 未保存状態/シーンパスが変化したらタイトルバーとLastScenePathを更新する ----
        {
            static bool s_lastDirty = false;
            static std::string s_lastTitlePath = ed ? ed->scenePath : scenePath;
            bool dirty = ed && ed->isDirty();
            const std::string& curPath = ed ? ed->scenePath : scenePath;
            if (dirty != s_lastDirty || curPath != s_lastTitlePath) {
                updateWindowTitle(window, curPath, dirty);
                if (curPath != s_lastTitlePath && !curPath.empty()) saveLastScenePath(curPath);
                s_lastDirty = dirty;
                s_lastTitlePath = curPath;
            }
        }

        // ---- エディターモード中は物理・スクリプトを止める ----
        if (isPlaying && !isPaused && !navMeshBusy) {
            FrameProfiler::get().beginSection("luau");
            luauEngine->resetFrameSafetyCounters();
            for (auto& [name, child] : system->getChildren()) {
                if (!child->IsA("Workspace")) continue;
                auto* ws = static_cast<Workspace*>(child.get());
                if (!ws->getPhysicsEngine()) ws->initPhysics();
                luauEngine->executeWorkspaceScripts(*ws);
                if (PathfindingService::IsBuildActive()) {
                    navMeshBusy = true;
                    break;
                }
                FrameProfiler::get().endSection("luau");
                FrameProfiler::get().beginSection("physics");
                ws->getPhysicsEngine()->update(*ws, deltaTime);
                FrameProfiler::get().endSection("physics");
                FrameProfiler::get().beginSection("luau");
            }
            if (!navMeshBusy) {
                luauEngine->executeSystemScripts();
                if (!PathfindingService::IsBuildActive()) {
                    luauEngine->fireHeartbeat(deltaTime);
                    luauEngine->update(deltaTime);
                } else {
                    navMeshBusy = true;
                }
            }
            FrameProfiler::get().endSection("luau");

            if (luauEngine->consumeSafetyHaltRequest()) {
                // Stopボタン(EditorManager.cpp)と同じ状態遷移
                ed->mode = EditorMode::Edit;
                if (user) user->controlMode = User::ControlMode::Free;
                RCBN_LOG("[INFO] Stopped due to safety limit breach. Switched to Free Camera mode.");
            }
        }
        // このフレームのScriptがFindPathを開始した場合、以降のゲーム更新を即座に止める。
        navMeshBusy = PathfindingService::IsBuildActive();

        // ---- 入力処理（エディターモードではカメラ操作のみ許可）----
        ViewportPanel* focusedVP = ed ? GetFocusedViewport() : nullptr;
        bool primaryFocused = focusedVP != nullptr && ed && focusedVP == ed->viewportPanel.get();
        state.viewportFocused    = primaryFocused;
        state.viewportZoomEnabled = primaryFocused || (ed && ed->viewportPanel && ed->viewportPanel->isHoveringViewport);
        if (!navMeshBusy) {
            user->processInput(workspace->getPhysicsEngine(), deltaTime,
                               state.viewportFocused, state.viewportZoomEnabled,
                               state.inputState == InputState::Gameplay,
                               ImGui::GetIO().WantTextInput);
        }
        if (user->consumeExitRequest()) {
            if (checkExit(ed, *window)) {
                break;
            }
        }

        // 再生中のAnimationを評価し、対象Cubeのcframeを上書きする
        // (processInput内のapplyBodyAnimationより後に行うことでアニメーションを優先させる)
        // workspace内の全Humanoid(NPC含む)が対象(旧: user->humanoidのみに限定されていた)
        if (isPlaying && !isPaused && !navMeshBusy) {
            Humanoid::updateAll(workspace.get(), deltaTime, workspace->getPhysicsEngine());
        }

        // Humanoidのパーツ配置(processInput内のapplyBodyAnimation)が終わった直後に、
        // アンカー駆動のキネマティックWeld(帽子等)を即時同期して追従ラグを無くす
        if (isPlaying && !isPaused && !navMeshBusy && workspace->getPhysicsEngine()) {
            workspace->getPhysicsEngine()->syncWeldKinematics();
        }

        // ---- Pキー: Workspace 切り替え ----
        if (!navMeshBusy && user->consumeWorkspaceSwitchRequest() && isPlaying) {
            // System直下のWorkspaceリストを収集
            std::vector<Workspace*> workspacePtrs;
            workspaces = SceneRuntime::collectWorkspaces(system);
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

        if (!navMeshBusy) beta({
            Vector3 centerPos = user->cpos;
            if (user->humanoid) {
                if (auto root = user->humanoid->getRootPart())
                    centerPos = root->getWorldCFrame().Position;
            }
            for (auto& [name, child] : workspace->getChildren()) {
                if (child->IsA("Terrain")) {
                    static_cast<Terrain*>(child.get())->update(centerPos);
                    break;
                }
            }
        });

        // ---- 天気更新（Edit/Play問わず常時。ParticleEmitter更新より前に風/発生源位置を反映） ----
        if (!navMeshBusy)
            Weather::updateAll(workspace.get(), deltaTime, user->cpos);

        // ---- パーティクル更新（Edit/Play問わず常時。Terrainと同じくアクティブworkspaceのみ） ----
        if (!navMeshBusy)
            ParticleEmitter::updateAll(workspace.get(), deltaTime);

        // ---- 描画 ----
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace.get());

        audioService->updateSounds(user->cpos, user->right);
        FrameProfiler::get().endFrame();

        // Detect transitions rather than held keys/buttons, so a held key
        // does not constantly wake the editor from energy-saving mode.
        bool editorInteraction = false;
        if (!windowInactive) {
            for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
                const unsigned char keyState =
                    glfwGetKey(window, key) == GLFW_PRESS ? GLFW_PRESS : GLFW_RELEASE;
                if (keyState != previousKeyState[key]) editorInteraction = true;
                previousKeyState[key] = keyState;
            }
            for (int button = 0; button < 3; ++button) {
                const unsigned char buttonState =
                    glfwGetMouseButton(window, button) == GLFW_PRESS ? GLFW_PRESS : GLFW_RELEASE;
                if (buttonState != previousMouseState[button]) editorInteraction = true;
                previousMouseState[button] = buttonState;
            }

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(window, &cursorX, &cursorY);
            if (cursorX != previousCursorX || cursorY != previousCursorY) editorInteraction = true;
            previousCursorX = cursorX;
            previousCursorY = cursorY;

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth != previousFramebufferWidth ||
                framebufferHeight != previousFramebufferHeight) {
                editorInteraction = true;
            }
            previousFramebufferWidth = framebufferWidth;
            previousFramebufferHeight = framebufferHeight;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (!windowInactive) {
            editorInteraction = editorInteraction || io.MouseDelta.x != 0.0f ||
                                io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f ||
                                io.MouseWheelH != 0.0f || io.InputQueueCharacters.Size > 0;
        }

        const double interactionTime = glfwGetTime();
        if (editorInteraction || isPlaying || navMeshBusy) lastEditorInteraction = interactionTime;

        const bool shouldSaveEnergy = !isPlaying &&
            (windowInactive || interactionTime - lastEditorInteraction >= editorIdleTimeoutSeconds);
        // if (shouldSaveEnergy != energySavingMode) {
        //     energySavingMode = shouldSaveEnergy;
        //     if (energySavingMode) {
        //         RCBN_LOG("[INFO] Editor entered energy-saving mode (15 FPS).");
        //     }
        //     else {
        //         RCBN_LOG("[INFO] Editor left energy-saving mode.");
        //     }
        // }
    }

    std::cout << "[DEBUG] Main loop ended.\n";
    RCBN_LOG("Shutting down...");

    savePanelVisibility(ed); // 次回起動時に復元できるようパネル開閉状態を保存
    saveEditorPreferences(ed, user.get()); // 次回起動時に復元できるようエディター環境設定を保存

    // windows(
    //     std::thread t([]() {
    //         MessageBoxW(
    //             NULL,
    //             L"アプリケーションを終了しています...",
    //             L"Info",
    //             MB_OK | MB_ICONINFORMATION
    //         );
    //     });
    //     t.detach();
    // )
    
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
    luauEngine->cancelAllTasks();
    // Terrainは各Workspaceの子インスタンスとして受け渡されるため、systemデストラクタで自動解放される
    // 全WorkspaceのPhysicsをクリア（m_ownedPhysics デストラクタで PxScene 解放）
    workspaces = SceneRuntime::collectWorkspaces(system);
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
