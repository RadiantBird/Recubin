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
#include <Instances/FontFile.hpp>
#include <Instances/Decal.hpp>
#include <Instances/SurfaceMark.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Weather.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/PathfindingService.hpp>
#include <Instances/ChatService.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/SceneRuntime.hpp>
#include <Core/FileLoader.hpp>
#include <Core/AudioService.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>

#include <Editor/EditorManager.hpp>
#include <Editor/GuiAutomation.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/Localization.hpp>
#include <Core/SystemState.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/Replication.hpp>
#include <include/imgui/imgui.h>

#include <Util/Logger.hpp>
#include <Util/FrameProfiler.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/RuntimeFileSystem.hpp>
#include <Util/RuntimeLaunchArgs.hpp>
#include <Util/UUID.hpp>
#include <Util/YamlLoadResult.hpp>

#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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
    glfwSwapInterval(1);

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

// シーン破棄の前に Terrain の streamer を明示的に解放する。
// Workspace のメンバ(m_ownedPhysics)は基底の children(Terrain) より先に破棄されるため、
// Workspace 破棄に任せると ~TerrainStreamer が解放済み Physics を参照してクラッシュする。
// ここで物理が生きているうちに reset し、ワーカー停止・GL/PhysX 解放・"terrain"フラッシュを完了させる。
static void resetTerrainStreamers(const std::vector<std::shared_ptr<Workspace>>& workspaces)
{
    SceneRuntime::releaseTerrainStreamers(workspaces);
}


// エディター設定（前回開いていたシーンパスなど）の保存先
static std::string kEditorSettingsPath = "editor_settings.yaml";
static bool g_editorSettingsLoadFailed = false;

// editor_settings.yaml 全体を読み込む（無ければ空ノード）
static YAML::Node loadEditorSettings() {
    if (!std::filesystem::exists(kEditorSettingsPath)) return YAML::Node();
    const YamlLoadResult loaded = loadYamlFile(kEditorSettingsPath);
    if (!loaded.success) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Failed to load editor settings: " << loaded.error);
        return YAML::Node();
    }
    if (loaded.node && !loaded.node.IsMap()) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid editor settings root: expected a YAML map");
        return YAML::Node();
    }
    return loaded.node;
}

// editor_settings.yaml 全体を書き出す（既存キーを保持してマージ保存するために使う）
static void writeEditorSettings(const YAML::Node& root) {
    const YamlSaveResult saved = saveYamlFileGuarded(
        kEditorSettingsPath, root, g_editorSettingsLoadFailed);
    if (!saved.success) RCBN_ERROR("Failed to save editor settings: " << saved.error);
}

// 前回開いていたシーンパスを読み込む。記録が無い/壊れている場合は空文字列を返す
static std::string loadLastScenePath() {
    YAML::Node root = loadEditorSettings();
    try {
        if (root && root["LastScenePath"]) return root["LastScenePath"].as<std::string>();
    } catch (const std::exception& error) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid LastScenePath in editor settings: " << error.what());
    }
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
    YAML::Node root = loadEditorSettings();
    YAML::Node p;
    try { p = root["Panels"]; }
    catch (const std::exception& error) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid Panels in editor settings: " << error.what());
        return;
    }
    auto get = [&](const char* key, bool def) -> bool {
        return (p && p[key]) ? p[key].as<bool>() : def;
    };
    try {
        ed->hierarchyPanel->isOpen      = get("Explorer",       true);
        ed->propertiesPanel->isOpen     = get("Properties",     true);
        ed->viewportPanel->isOpen       = get("Viewport",       true);
        ed->contentBrowserPanel->isOpen = get("ContentBrowser", true);
        ed->consolePanel->isOpen        = get("Console",        true);
        ed->animationPanel->isOpen      = get("Animation",      false);
    } catch (const std::exception& error) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid panel visibility in editor settings: " << error.what());
    }
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
    YAML::Node root = loadEditorSettings();
    YAML::Node p;
    try { p = root["Preferences"]; }
    catch (const std::exception& error) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid Preferences in editor settings: " << error.what());
        return;
    }
    if (!p) return;

    try {

    if (p["PhysicsDebug"]) ed->viewportPanel->showPhysicsDebug = p["PhysicsDebug"].as<bool>();
    if (p["RenderingDebug"]) ed->viewportPanel->showRenderingDebug = p["RenderingDebug"].as<bool>();

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
    if (p["GizmoSize"])        user->gizmoSize = std::clamp(p["GizmoSize"].as<float>(), 0.05f, 0.50f);

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

    if (p["PlayMode"]) {
        const std::string playMode = p["PlayMode"].as<std::string>();
        if (playMode == "PlayHere") ed->setSelectedPlayMode(EditorPlayMode::PlayHere);
        else if (playMode == "LocalServer") ed->setSelectedPlayMode(EditorPlayMode::LocalServer);
        else ed->setSelectedPlayMode(EditorPlayMode::Normal);
    }
    if (p["NetworkClientCount"])
        ed->setNetworkClientCount(std::clamp(p["NetworkClientCount"].as<int>(), 1, 8));
    } catch (const std::exception& error) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid Preferences in editor settings: " << error.what());
    } catch (...) {
        g_editorSettingsLoadFailed = true;
        RCBN_ERROR("Invalid Preferences in editor settings: unknown error");
    }
}

// エディター環境設定を editor_settings.yaml へ保存する（他キーは保持）
static void saveEditorPreferences(EditorManager* ed, User* user) {
    if (!ed || !user) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;

    p["PhysicsDebug"] = ed->viewportPanel->showPhysicsDebug;
    p["RenderingDebug"] = ed->viewportPanel->showRenderingDebug;
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
    p["GizmoSize"]        = std::clamp(user->gizmoSize, 0.05f, 0.50f);

    p["GizmoOp"]     = static_cast<int>(ed->viewportPanel->gizmoOp);
    p["GizmoMode"]   = static_cast<int>(ed->viewportPanel->gizmoMode);
    p["SelectOnly"]  = ed->viewportPanel->selectOnly;
    p["ToolNone"]    = ed->viewportPanel->toolNone;

    const char* playMode = "Normal";
    switch (ed->selectedPlayMode()) {
        case EditorPlayMode::Normal:      playMode = "Normal"; break;
        case EditorPlayMode::PlayHere:    playMode = "PlayHere"; break;
        case EditorPlayMode::LocalServer: playMode = "LocalServer"; break;
    }
    p["PlayMode"] = playMode;
    p["NetworkClientCount"] = std::clamp(ed->networkClientCount(), 1, 8);

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
    if (!audioService->initialize()) {
        std::cerr << "[gen-test-scene] ERROR: AudioService initialization failed\n";
        return 1;
    }

    auto system    = std::make_shared<System>();
    auto workspace = std::make_shared<Workspace>();
    system->addChild(workspace);

    // SceneLoader::createInstance() が対応する全クラス（System/Workspace/
    // PathfindingService/Script は特殊なため除外。Script は末尾で検証用に個別追加する）。
    // createInstance() に新しいクラスを追加した場合はここにも追加すること。
    static const char* kClassNames[] = {
        "Cube", "Cylinder", "TriangularPrism", "Truss", "Seat", "Sphere", "MeshCube", "LiquidCube", "SpawnLocation",
        "Skybox", "Sun", "Moon", "Model", "Sound", "SurfaceMark",
        "Lighting", "PointLight", "SpotLight", "PostEffect",
        "AppImage", "Humanoid", "Animation", "StarterCharacter", "Terrain", "Instance",
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
    for (const auto& type : PhysicalFileInstanceRegistry::types()) {
        auto inst = PhysicalFileInstanceRegistry::create(type.className);
        if (!inst) {
            std::cout << "[gen-test-scene] WARNING: failed to create "
                      << type.className << "\n";
            continue;
        }
        inst->Name = std::string(type.className);
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
    GuiAutomation::configureFromArgs(argc, argv);
    const RuntimeLaunchArgs runtimeArgs = parseRuntimeLaunchArgs(argc, argv);
    if (!runtimeArgs.valid) {
        RCBN_ERROR("Invalid runtime arguments: " << runtimeArgs.error);
        return -1;
    }
    const bool uiAutomationScene = runtimeArgs.uiAutomationScene.has_value() ||
                                   runtimeArgs.uiAutomationSettings.has_value();
    if (uiAutomationScene && !GuiAutomation::enabled()) {
        RCBN_ERROR("UI automation fixture arguments require --ui-automation");
        return -1;
    }
    if (uiAutomationScene &&
        (!runtimeArgs.uiAutomationScene.has_value() ||
         !runtimeArgs.uiAutomationSettings.has_value())) {
        RCBN_ERROR("--ui-automation-scene and --ui-automation-settings must be provided together");
        return -1;
    }
    if (uiAutomationScene) {
        std::error_code sceneError;
        if (!std::filesystem::is_regular_file(*runtimeArgs.uiAutomationScene, sceneError)) {
            RCBN_ERROR("Invalid --ui-automation-scene path: " << *runtimeArgs.uiAutomationScene);
            return -1;
        }
        kEditorSettingsPath = *runtimeArgs.uiAutomationSettings;
    }
    // コンソールの出力/入力コードページをUTF-8にする
    // (Windows日本語版等では既定のANSIコードページのままだと、UTF-8で書かれた
    //  ログやLuauのprint出力が文字化けする)
    getPlatform().setupConsoleUtf8();
    GuiAutomation::start();
    getPlatform().setupDllSearchPath();
    if (!Physics::configureBackendFromCommandLine(argc, argv)) return -1;

    std::cout << "Hello world!\n"
              << "Recubin Studio v0.9989\n";
    std::filesystem::path engineExePath = (argc > 0 && argv[0]) ? std::filesystem::path(argv[0]) : std::filesystem::path();

    windows(
        DWORD myPid = GetCurrentProcessId();
        std::string cmd = "Watcher.exe " + std::to_string(myPid);

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};

        // ウォッチドッグを起動(エラー情報の収集用)
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
    std::string scenePath = runtimeArgs.uiAutomationScene.value_or(""); // 空 = 無題
    SceneLoader::SceneDocumentMetadata initialSceneMetadata;
    updateWindowTitle(window, scenePath);

    {
        auto bound = SceneRuntime::loadAndBind(scenePath, system, user, *luauEngine, window);
        workspace  = bound.workspace;
        workspaces = bound.workspaces;
        initialSceneMetadata = bound.metadata;
        if (RecubinUUID::isValid(system->ApplicationId)) {
            luauEngine->setRuntimeFileSystem(std::make_shared<RuntimeFileSystem>(
                system->ApplicationId, RuntimeFileSystem::Namespace::Editor,
                system->EnableExternalFileAccess));
        }
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
    ed->setSceneMetadata(initialSceneMetadata);
    if (initialSceneMetadata.applicationIdGenerated) ed->markDirty();
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

    double lastFrame = glfwGetTime();
    bool wasPlaying = false;
    bool snapshotDirty = false;
    SceneLoader::SceneDocumentMetadata originalSceneMetadata;
    const std::string snapshotPath = "assets/scenes/_snapshot.yaml";

    struct ManagedNetworkClient {
        std::unique_ptr<IChildProcess> process;
        std::filesystem::path logPath;
        bool exitReported = false;
    };
    std::vector<ManagedNetworkClient> networkClients;
    std::unique_ptr<ReplicationManager> editorReplication;
    std::shared_ptr<ChatService> editorChatService;
    bool networkClientCleanupActive = false;
    double networkClientCleanupDeadline = 0.0;

    auto clearEditorNetwork = [&] {
        auto& network = NetworkManager::get();
        network.onRoleChanged = nullptr;
        network.onGameMessage = nullptr;
        network.onChatMessage = nullptr;
        if (editorChatService) editorChatService->onSendRequested = nullptr;
        renderer->setChatService(nullptr);
        editorChatService.reset();
        editorReplication.reset();
        network.shutdown();
        if (ed) ed->setNetworkClientStatus(0, 0);
    };

    auto beginNetworkClientCleanup = [&] {
        if (networkClients.empty()) return;
        for (auto& client : networkClients) {
            if (client.process && client.process->isRunning()) client.process->requestClose();
        }
        networkClientCleanupActive = true;
        networkClientCleanupDeadline = glfwGetTime() + 2.0;
        if (ed) ed->setExternalPlayCleanup(true);
    };

    auto pollNetworkClientCleanup = [&](double now) {
        if (!networkClientCleanupActive) return;
        bool allStopped = true;
        for (auto& client : networkClients) {
            if (!client.process || !client.process->isRunning()) continue;
            allStopped = false;
            if (now >= networkClientCleanupDeadline) {
                client.process->terminate();
            }
        }
        if (allStopped) {
            networkClients.clear();
            networkClientCleanupActive = false;
            if (ed) ed->setExternalPlayCleanup(false);
        }
    };

    auto restoreObserverBinding = [&] {
        std::shared_ptr<Instance> usersContainer;
        for (auto& [name, child] : system->getChildren()) {
            if (child && child->IsA("Users")) {
                usersContainer = child;
                break;
            }
        }
        if (usersContainer && user->Parent.expired()) usersContainer->addChild(user);
        luauEngine->setGlobalInstance("User", user);
        user->controlMode = User::ControlMode::Free;
    };

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

    auto installBoundScene = [&](SceneRuntime::Bound bound, bool isDirty) {
        workspace  = bound.workspace;
        workspaces = bound.workspaces;
        if (system && RecubinUUID::isValid(system->ApplicationId)) {
            luauEngine->setRuntimeFileSystem(std::make_shared<RuntimeFileSystem>(
                system->ApplicationId, RuntimeFileSystem::Namespace::Editor,
                system->EnableExternalFileAccess));
        }
        ed->setSceneMetadata(bound.metadata);
        ed->setWorkspace(workspace.get());
        if (isDirty || bound.metadata.applicationIdGenerated) ed->markDirty();
        if (workspace) workspace->initPhysics();
    };

    auto initNewScene = [&](const std::string& path, bool isDirty) {
        installBoundScene(
            SceneRuntime::loadAndBind(path, system, user, *luauEngine, window),
            isDirty);
    };

    auto restorePlaySnapshot = [&] {
        audioService->stopAllSounds();
        user->despawnCharacter();
        // Undo/Clipboardが旧WorkspaceのInstanceを保持したままPhysicsを破棄しないよう、
        // 通常Stopと起動失敗の両方をこの順序へ集約する。
        if (ed) {
            ed->hierarchyPanel->selectedInstance = nullptr;
            ed->m_history.clear();
            ed->clearClipboard();
            ed->animationPanel->endEditSession();
        }
        workspaces = SceneRuntime::collectWorkspaces(system);
        luauEngine->cancelAllTasks();
        resetTerrainStreamers(workspaces);
        clearWorkspacePhysics(workspaces);
        initNewScene(snapshotPath, snapshotDirty);
        if (ed) ed->setSceneMetadata(originalSceneMetadata);
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
        double currentFrame = glfwGetTime();
        float deltaTime = static_cast<float>(std::max(0.0, currentFrame - lastFrame));
        lastFrame          = currentFrame;
        pollNetworkClientCleanup(currentFrame);

        SystemState& state = SystemState::get();
        state.deltaTime = deltaTime;

        // ワールド更新が停止中でも、非同期FindPathの完了だけは毎フレーム処理する。
        luauEngine->pollPathfindingRequests();
        bool navMeshBusy = PathfindingService::IsBuildActive();

        state.isPlaying  = ed && !ed->isEditMode();
        state.isPaused   = ed &&  ed->isPauseMode();
        state.inputState = state.isPlaying ? InputState::Gameplay : InputState::Editor;

        const bool isPlaying = state.isPlaying;
        const bool isPaused  = state.isPaused;

        // ---- Play/Stop 遷移処理 ----
        bool playTransitionAccepted = true;
        if (isPlaying && !wasPlaying) {
            const EditorPlayMode playMode = ed->activePlayMode();
            const std::optional<Vector3> playHerePosition =
                playMode == EditorPlayMode::PlayHere
                    ? std::optional<Vector3>(user->getCameraCFrame().Position)
                    : std::nullopt;
            if (playMode == EditorPlayMode::LocalServer && !system->UseNetwork) {
                ed->showLocalServerNetworkRequiredError();
                ed->mode = EditorMode::Edit;
                user->controlMode = User::ControlMode::Free;
                state.isPlaying = false;
                state.inputState = InputState::Editor;
                playTransitionAccepted = false;
            }

            if (playTransitionAccepted) {
            // SlotsはInventoryから導出される実行時キャッシュ。EditorでToolを移動した
            // 直後でも前回のshared_ptrを持ち越さないよう、Play開始時に実体から再構築する。
            user->syncToolsFromInventory();
            // System配下(Workspace外)のスクリプトはPlay/Stopで破棄されないため、
            // 前回Playの実行状態(Completed等)をリセットして毎回最初から実行させる
            luauEngine->resetSystemScripts();
            snapshotDirty = ed && ed->isDirty();
            originalSceneMetadata = ed ? ed->sceneMetadata() : SceneLoader::SceneDocumentMetadata{};
            // Animation Editorの編集セッションが開いたままだと、リグ/プレビュー姿勢が
            // スナップショットに焼き込まれ、Stop後のシーンが静かに汚染される。保存前に必ず復元する
            if (ed && ed->animationPanel) ed->animationPanel->endEditSession();
            auto snapshotMetadata = ed ? ed->sceneMetadata() : SceneLoader::SceneDocumentMetadata{};
            SceneLoader::saveSceneResult(system.get(), snapshotPath, snapshotMetadata);
            SceneLoader::resolveConstraintRefs(system.get());
            // 全WorkspaceのPhysicsを初期化
            for (auto& [name, child] : system->getChildren()) {
                if (child->IsA("Workspace")) {
                    auto* ws = static_cast<Workspace*>(child.get());
                    if (!ws->getPhysicsEngine()) ws->initPhysics();
                }
            }

            if (playMode == EditorPlayMode::LocalServer) {
                std::error_code pathError;
                std::filesystem::path studioPath = std::filesystem::absolute(engineExePath, pathError);
                if (pathError || studioPath.empty()) {
                    ed->showPlayStartError("Failed to resolve the editor executable path.");
                    playTransitionAccepted = false;
                }
                std::filesystem::path enginePath;
                if (playTransitionAccepted) {
#ifdef _WIN32
                    enginePath = studioPath.parent_path() / "RecubinEngine.exe";
#else
                    enginePath = studioPath.parent_path() / "RecubinEngine";
#endif
                    pathError.clear();
                    if (!std::filesystem::is_regular_file(enginePath, pathError) || pathError) {
                        ed->showPlayStartError("RecubinEngine was not found next to the editor executable.");
                        playTransitionAccepted = false;
                    }
                }

                std::filesystem::path workingDirectory;
                std::filesystem::path logDirectory;
                if (playTransitionAccepted) {
                    pathError.clear();
                    workingDirectory = std::filesystem::current_path(pathError);
                    if (pathError) {
                        ed->showPlayStartError("Failed to resolve the project working directory.");
                        playTransitionAccepted = false;
                    }
                }
                if (playTransitionAccepted) {
                    pathError.clear();
                    const auto sessionId = std::chrono::steady_clock::now().time_since_epoch().count();
                    logDirectory = std::filesystem::temp_directory_path(pathError) /
                        "RecubinStudio" / ("local-server-" + std::to_string(sessionId));
                    if (!pathError) std::filesystem::create_directories(logDirectory, pathError);
                    if (pathError) {
                        ed->showPlayStartError("Failed to create the client log directory.");
                        playTransitionAccepted = false;
                    }
                }

                if (playTransitionAccepted && !NetworkManager::get().startHost(0, false)) {
                    ed->showPlayStartError("Failed to start the local direct server.");
                    playTransitionAccepted = false;
                }

                if (playTransitionAccepted) {
                    user->controlMode = User::ControlMode::Free;
                    user->despawnCharacter();
                    if (auto parent = user->Parent.lock()) parent->removeChild(user->Name);
                    luauEngine->clearGlobalInstance("User");

                    if (auto it = system->children.find("ChatService"); it != system->children.end())
                        editorChatService = std::dynamic_pointer_cast<ChatService>(it->second);
                    if (editorChatService) {
                        editorChatService->onSendRequested = [](const std::string& text) {
                            NetworkManager::get().sendChatMessage(text);
                        };
                        renderer->setChatService(editorChatService);
                    }

                    editorReplication = std::make_unique<ReplicationManager>(workspace, user, system.get());
                    NetworkManager::get().onGameMessage = [&](uint8_t type, const uint8_t* payload,
                                                               size_t len, PeerId senderId) {
                        if (editorReplication)
                            editorReplication->onGameMessage(type, payload, len, senderId);
                    };
                    NetworkManager::get().onChatMessage = [&](PeerId senderId, const std::string& text) {
                        RCBN_LOG("[Chat][Peer " << senderId << "] " << text);
                        if (editorChatService) {
                            editorChatService->receiveMessage(senderId, text);
                            luauEngine->fireChatMessage(editorChatService.get(), senderId, text);
                        }
                    };
                    NetworkManager::get().onRoleChanged = [&](NetworkRole oldRole, NetworkRole newRole) {
                        RCBN_LOG("NetworkManager: role changed " << NetworkManager::roleToString(oldRole)
                                  << " -> " << NetworkManager::roleToString(newRole));
                        luauEngine->fireNetworkRoleChanged(oldRole, newRole);
                        if (editorReplication)
                            editorReplication->onNetworkRoleChanged(oldRole, newRole);
                    };

                    const uint16_t port = NetworkManager::get().getListenPort();
                    const int clientCount = std::clamp(ed->networkClientCount(), 1, 8);
                    ed->setNetworkClientStatus(0, clientCount);
                    for (int clientIndex = 1; clientIndex <= clientCount; ++clientIndex) {
                        const std::filesystem::path logPath =
                            logDirectory / ("client-" + std::to_string(clientIndex) + ".log");
                        ChildProcessLaunchOptions options;
                        options.executable = enginePath.string();
                        options.arguments = {
                            "--scene", snapshotPath,
                            "--direct-connect", "127.0.0.1:" + std::to_string(port),
                            "--listen-port", "0",
                            "--window-title", "Client " + std::to_string(clientIndex),
                            "--editor-test"
                        };
                        options.workingDirectory = workingDirectory.string();
                        options.outputLogPath = logPath.string();
                        auto process = getPlatform().launchChildProcess(options);
                        if (!process) {
                            ed->showPlayStartError(
                                "Failed to launch Client " + std::to_string(clientIndex) + ".");
                            playTransitionAccepted = false;
                            break;
                        }
                        networkClients.push_back({std::move(process), logPath, false});
                    }
                }

                if (!playTransitionAccepted) {
                    beginNetworkClientCleanup();
                    clearEditorNetwork();
                    restoreObserverBinding();
                    restorePlaySnapshot();
                    ed->mode = EditorMode::Edit;
                    state.isPlaying = false;
                    state.inputState = InputState::Editor;
                }
            }

            if (playTransitionAccepted) {
                // 先にスクリプトを実行開始する。専用サーバーではNetworkRoleにより
                // LocalScriptが除外され、通常のScriptだけが実行される。
                for (auto& [name, child] : system->getChildren()) {
                    if (child->IsA("Workspace")) {
                        auto* ws = static_cast<Workspace*>(child.get());
                        luauEngine->executeWorkspaceScripts(*ws);
                    }
                }
                luauEngine->executeSystemScripts();

                if (playMode != EditorPlayMode::LocalServer) {
                    // CharacterAddedからも初期座標が見えるよう、Play Hereの位置は
                    // spawnCharacterへ直接渡す。
                    user->spawnCharacter(system.get(), workspace.get(), playHerePosition);
                    audioService->playAutoPlaySounds();
                    if (user->character) workspace->addChild(user->character);
                }
            }
            }
        }
        if (!isPlaying && wasPlaying) {
            if (ed->activePlayMode() == EditorPlayMode::LocalServer) {
                beginNetworkClientCleanup();
                clearEditorNetwork();
            }
            restorePlaySnapshot();
        }
        wasPlaying = isPlaying && playTransitionAccepted;

        // ---- シーンのリロード（Loadボタン / 新規作成）----
        if (ed && (ed->pendingNewScene || !ed->pendingLoadPath.empty()) && ed->isEditMode()) {
            bool isNewScene = ed->pendingNewScene;
            std::string loadPath = isNewScene ? std::string() : ed->pendingLoadPath;
            auto staged = SceneRuntime::stageSceneLoad(loadPath, system, user);
            if (!staged) {
                ed->showSceneLoadError(staged.message.empty()
                    ? "Scene could not be loaded" : staged.message);
                ed->pendingNewScene = false;
                ed->pendingLoadPath.clear();
                continue;
            }
            ed->pendingNewScene = false;
            ed->pendingLoadPath.clear();
            ed->scenePath = loadPath;

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

            installBoundScene(SceneRuntime::commitAndBind(
                std::move(staged), system, user, *luauEngine, window), false);
            ed->evaluateSceneMigration();
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

        bool runtimeFrameOk = playTransitionAccepted;
        const bool localServerFrame = isPlaying && playTransitionAccepted && ed &&
            ed->activePlayMode() == EditorPlayMode::LocalServer;
        if (localServerFrame) {
            user->controlMode = User::ControlMode::Free;
            NetworkManager::get().update(deltaTime);

            int connectedPlayers = 0;
            for (const PeerInfo& peer : NetworkManager::get().getRoster()) {
                if (peer.isPlayer) ++connectedPlayers;
            }
            ed->setNetworkClientStatus(connectedPlayers,
                                       std::clamp(ed->networkClientCount(), 1, 8));

            for (auto& client : networkClients) {
                if (!client.process || client.exitReported || client.process->isRunning()) continue;
                const std::optional<int> exitCode = client.process->exitCode();
                RCBN_LOG("[Network Test] Client exited"
                         << (exitCode ? " with code " + std::to_string(*exitCode) : std::string())
                         << ". Log: " << client.logPath.string());
                client.exitReported = true;
            }

            const bool networkFailed = !NetworkManager::get().isActive() ||
                NetworkManager::get().getConnectionState() == ConnectionState::Failed;
            if (networkFailed) {
                const ConnectionError error = NetworkManager::get().getConnectionError();
                ed->showPlayStartError(
                    std::string("Local server failed: ") +
                    NetworkManager::connectionErrorToString(error));
                ed->mode = EditorMode::Edit;
                user->controlMode = User::ControlMode::Free;
                state.isPlaying = false;
                state.inputState = InputState::Editor;
                runtimeFrameOk = false;
            } else if (!navMeshBusy && editorReplication) {
                editorReplication->update(deltaTime, workspace->getPhysicsEngine());
                if (editorReplication->hasFatalIdentityError()) {
                    ed->showPlayStartError("Local server replication failed.");
                    ed->mode = EditorMode::Edit;
                    user->controlMode = User::ControlMode::Free;
                    state.isPlaying = false;
                    state.inputState = InputState::Editor;
                    runtimeFrameOk = false;
                }
            }
        }

        // ---- エディターモード中は物理・スクリプトを止める ----
        if (isPlaying && runtimeFrameOk && !isPaused && !navMeshBusy) {
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
        state.viewportHovered = ed && ed->viewportPanel && ed->viewportPanel->isHoveringViewport;
        if (!navMeshBusy) {
            user->processInput(workspace->getPhysicsEngine(), deltaTime,
                               state.viewportFocused, state.viewportHovered,
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
        if (isPlaying && runtimeFrameOk && !isPaused && !navMeshBusy) {
            Humanoid::updateAll(workspace.get(), deltaTime, workspace->getPhysicsEngine());
        }

        // Humanoidのパーツ配置(processInput内のapplyBodyAnimation)が終わった直後に、
        // アンカー駆動のキネマティックWeld(帽子等)を即時同期して追従ラグを無くす
        if (isPlaying && runtimeFrameOk && !isPaused && !navMeshBusy && workspace->getPhysicsEngine()) {
            workspace->getPhysicsEngine()->syncWeldKinematics();
        }

        // ---- Pキー: Workspace 切り替え ----
        if (!navMeshBusy && runtimeFrameOk && user->consumeWorkspaceSwitchRequest() && isPlaying) {
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
                    if (editorReplication) editorReplication->setWorkspace(workspace);
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
            SceneRuntime::updateTerrains(workspace.get(), centerPos);
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

    // LocalServerの子プロセスはGLFW/GLを破棄する前に閉じる。通常終了に最大2秒を
    // 与えた後、残ったプロセスだけを強制終了する。
    for (auto& client : networkClients) {
        if (client.process && client.process->isRunning()) client.process->requestClose();
    }
    const auto childExitDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!networkClients.empty() && std::chrono::steady_clock::now() < childExitDeadline) {
        bool allStopped = true;
        for (auto& client : networkClients) {
            if (client.process && client.process->isRunning()) {
                allStopped = false;
                break;
            }
        }
        if (allStopped) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (auto& client : networkClients) {
        if (client.process && client.process->isRunning()) client.process->terminate();
    }
    networkClients.clear();
    networkClientCleanupActive = false;
    if (ed) ed->setExternalPlayCleanup(false);
    clearEditorNetwork();

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
