#ifdef _WIN32
    #include <windows26.h>
#endif

#include "Studio.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Core/AudioService.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/SceneRuntime.hpp>
#include <Core/SystemState.hpp>
#include <Editor/EditorManager.hpp>
#include <Editor/GuiAutomation.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/Localization.hpp>
#include <Instances/ChatService.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/System.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Model.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/PathfindingService.hpp>
#include <Instances/Weather.hpp>
#include <Instances/Workspace.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/Replication.hpp>
#include <Util/AssetPath.hpp>
#include <Util/FrameProfiler.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Platform.hpp>
#include <Util/YamlLoadResult.hpp>
#include <Util/Logger.hpp>
#include <Util/RuntimeFileSystem.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <optional>
#include <thread>
#include <vector>

namespace StudioMain {
namespace {

void migrateLegacyLighting(
    const std::shared_ptr<System>& system,
    const std::shared_ptr<Workspace>& workspace,
    const std::vector<std::shared_ptr<Workspace>>& workspaces)
{
    // @RadiantBird 2026/09/14: 古い YAML 形式との移行用。形式整理時に削除予定。
    for (auto it = system->children.begin(); it != system->children.end();) {
        if (!it->second->IsA("Lighting")) {
            ++it;
            continue;
        }

        const auto oldLighting = std::static_pointer_cast<Lighting>(it->second);
        auto lighting = std::make_shared<Lighting>();
        lighting->Name = oldLighting->Name;
        lighting->legacyDirection = oldLighting->legacyDirection;
        lighting->brightness = oldLighting->brightness;
        it = system->children.erase(it);
        workspace->addChild(lighting);
        break;
    }

    for (const auto& currentWorkspace : workspaces) {
        SceneRuntime::migrateLegacyLightingDirection(*currentWorkspace);
    }
}

GLFWwindow* createWindow()
{
#ifdef _WIN32
    // GLFW/ImGui が論理座標と Framebuffer 座標を正しく扱えるよう生成前に設定する。
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }
#endif
    if (!glfwInit()) {
        RCBN_ERROR("GLFW initialization failed.");
        return nullptr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    if (GuiAutomation::hiddenWindow()) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(1600, 900, "Recubin Studio", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        RCBN_ERROR("Studio window creation failed.");
        return nullptr;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glfwDestroyWindow(window);
        glfwTerminate();
        RCBN_ERROR("GLEW initialization failed.");
        return nullptr;
    }
    return window;
}

class EditorSettings {
public:
    explicit EditorSettings(const std::filesystem::path& path)
        : m_path(AssetPath::toStored(path))
    {
    }

    std::string loadLastScenePath();
    void saveLastScenePath(const std::string& path);
    void loadPanelVisibility(EditorManager* editor);
    void savePanelVisibility(EditorManager* editor);
    void loadEditorPreferences(EditorManager* editor, User* user, GLFWwindow* window);
    void saveEditorPreferences(EditorManager* editor, User* user);

private:
    YAML::Node loadEditorSettings();
    void writeEditorSettings(const YAML::Node& root);

    void showInternalIoFailureOnce(const std::string& operation,
                                   const std::string& path,
                                   const std::string& reason)
    {
        if (m_ioFailures.insert(operation).second) {
            showInternalIoFailure(operation, AssetPath::fromStored(path), reason);
        }
    }

    void clearInternalIoFailure(const std::string& operation, const std::string&)
    {
        m_ioFailures.erase(operation);
    }

    std::string m_path;
    bool m_loadFailed = false;
    std::unordered_set<std::string> m_ioFailures;
};

// editor_settings.yaml 全体を読み込む（無ければ空ノード）
YAML::Node EditorSettings::loadEditorSettings() {
    std::error_code inspectError;
    if (!std::filesystem::exists(m_path, inspectError)) {
        if (inspectError) {
            showInternalIoFailureOnce("Read editor settings", m_path,
                                      inspectError.message());
        } else clearInternalIoFailure("Read editor settings", m_path);
        return YAML::Node();
    }
    const YamlLoadResult loaded = loadYamlFile(m_path);
    if (!loaded.success) {
        m_loadFailed = true;
        RCBN_ERROR("Failed to load editor settings: " << loaded.error);
        if (loaded.failureKind == YamlFailureKind::Io)
            showInternalIoFailureOnce("Read editor settings", m_path, loaded.error);
        return YAML::Node();
    }
    clearInternalIoFailure("Read editor settings", m_path);
    if (loaded.node && !loaded.node.IsMap()) {
        m_loadFailed = true;
        RCBN_ERROR("Invalid editor settings root: expected a YAML map");
        return YAML::Node();
    }
    return loaded.node;
}

// editor_settings.yaml 全体を書き出す（既存キーを保持してマージ保存するために使う）
void EditorSettings::writeEditorSettings(const YAML::Node& root) {
    const YamlSaveResult saved = saveYamlFileGuarded(
        m_path, root, m_loadFailed);
    if (!saved.success) {
        RCBN_ERROR("Failed to save editor settings: " << saved.error);
        if (saved.failureKind == YamlFailureKind::Io)
            showInternalIoFailureOnce("Write editor settings", m_path, saved.error);
    } else clearInternalIoFailure("Write editor settings", m_path);
}

// 前回開いていたシーンパスを読み込む。記録が無い/壊れている場合は空文字列を返す
std::string EditorSettings::loadLastScenePath() {
    YAML::Node root = loadEditorSettings();
    try {
        if (root && root["LastScenePath"]) return root["LastScenePath"].as<std::string>();
    } catch (const std::exception& error) {
        m_loadFailed = true;
        RCBN_ERROR("Invalid LastScenePath in editor settings: " << error.what());
    }
    return "";
}

// 次回起動時に自動で開けるよう、現在のシーンパスを記録する（他キーは保持）
void EditorSettings::saveLastScenePath(const std::string& path) {
    YAML::Node root = loadEditorSettings();
    root["LastScenePath"] = path;
    writeEditorSettings(root);
}

// 各パネルの開閉状態を editor_settings.yaml から復元する。
// 記録が無い場合は既定（Animation/Profiler は非表示、他は表示）を使う。
void EditorSettings::loadPanelVisibility(EditorManager* ed) {
    if (!ed) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;
    try { p = root["Panels"]; }
    catch (const std::exception& error) {
        m_loadFailed = true;
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
        ed->profilerPanel->isOpen       = get("Profiler",       false);
        ed->audioDebugPanel->isOpen     = get("AudioDebug",     false);
    } catch (const std::exception& error) {
        m_loadFailed = true;
        RCBN_ERROR("Invalid panel visibility in editor settings: " << error.what());
    }
}

// 各パネルの開閉状態を editor_settings.yaml へ保存する（他キーは保持）
void EditorSettings::savePanelVisibility(EditorManager* ed) {
    if (!ed) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;
    p["Explorer"]       = ed->hierarchyPanel->isOpen;
    p["Properties"]     = ed->propertiesPanel->isOpen;
    p["Viewport"]       = ed->viewportPanel->isOpen;
    p["ContentBrowser"] = ed->contentBrowserPanel->isOpen;
    p["Console"]        = ed->consolePanel->isOpen;
    p["Animation"]      = ed->animationPanel->isOpen;
    p["Profiler"]       = ed->profilerPanel->isOpen;
    p["AudioDebug"]     = ed->audioDebugPanel->isOpen;
    root["Panels"] = p;
    writeEditorSettings(root);
}

// エディター環境設定（物理デバッグ表示・言語・カメラ・スナップ/フィット・ギズモモード）を
// editor_settings.yaml から復元する。記録が無い/壊れている場合は既定値のまま変更しない。
void EditorSettings::loadEditorPreferences(
    EditorManager* ed, User* user, GLFWwindow* window) {
    if (!ed || !user) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;
    try { p = root["Preferences"]; }
    catch (const std::exception& error) {
        m_loadFailed = true;
        RCBN_ERROR("Invalid Preferences in editor settings: " << error.what());
        return;
    }
    if (!p) return;

    try {

    if (p["PhysicsDebug"]) ed->viewportPanel->showPhysicsDebug = p["PhysicsDebug"].as<bool>();
    if (p["RenderingDebug"]) ed->viewportPanel->showRenderingDebug = p["RenderingDebug"].as<bool>();
    if (p["VSync"]) ed->setVSyncEnabled(p["VSync"].as<bool>(), window);

    if (p["Language"]) {
        std::string lang = p["Language"].as<std::string>();
        if (lang == "JA") Loc::setLanguage(Loc::Lang::JA);
        else if (lang == "EN") Loc::setLanguage(Loc::Lang::EN);
    }

    if (p["Theme"]) {
        std::string theme = p["Theme"].as<std::string>();
        if (theme == "Frutiger") ed->setTheme(EditorTheme::Frutiger);
        else if (theme == "Classic") ed->setTheme(EditorTheme::Classic);
    }

    if (p["Camera"]) {
        YAML::Node c = p["Camera"];
        if (c["Pos"] && c["Pos"].IsSequence() && c["Pos"].size() == 3 &&
            c["Rot"] && c["Rot"].IsSequence() && c["Rot"].size() == 4) {
            Vector3 pos(c["Pos"][0].as<float>(), c["Pos"][1].as<float>(), c["Pos"][2].as<float>());
            Quaternion rot = Quaternion();
            {
                Quaternion q = Quaternion();
                q.tryFromComponents(
                    c["Rot"][3].as<float>(), // w
                    c["Rot"][0].as<float>(), // x
                    c["Rot"][1].as<float>(), // y
                    c["Rot"][2].as<float>(), // z
                    rot
                );
            }
            
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
    if (p["SurfaceDrag"])      ed->viewportPanel->surfaceDrag      = p["SurfaceDrag"].as<bool>();
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
    if (p["MultiResizeMode"]) {
        const std::string mode = p["MultiResizeMode"].as<std::string>();
        ed->viewportPanel->multiResizeMode =
            mode == "GroupScale" ? ViewportPanel::MultiResizeMode::GroupScale
                                  : ViewportPanel::MultiResizeMode::Individual;
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
        m_loadFailed = true;
        RCBN_ERROR("Invalid Preferences in editor settings: " << error.what());
    } catch (...) {
        m_loadFailed = true;
        RCBN_ERROR("Invalid Preferences in editor settings: unknown error");
    }
}

// エディター環境設定を editor_settings.yaml へ保存する（他キーは保持）
void EditorSettings::saveEditorPreferences(EditorManager* ed, User* user) {
    if (!ed || !user) return;
    YAML::Node root = loadEditorSettings();
    YAML::Node p;

    p["PhysicsDebug"] = ed->viewportPanel->showPhysicsDebug;
    p["RenderingDebug"] = ed->viewportPanel->showRenderingDebug;
    p["VSync"] = ed->vsyncEnabled();
    p["Language"] = (Loc::getLanguage() == Loc::Lang::JA) ? std::string("JA") : std::string("EN");
    p["Theme"] = (ed->theme() == EditorTheme::Frutiger) ? std::string("Frutiger") : std::string("Classic");

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
    p["SurfaceDrag"]      = ed->viewportPanel->surfaceDrag;
    p["GizmoSize"]        = std::clamp(user->gizmoSize, 0.05f, 0.50f);

    p["GizmoOp"]     = static_cast<int>(ed->viewportPanel->gizmoOp);
    p["GizmoMode"]   = static_cast<int>(ed->viewportPanel->gizmoMode);
    p["MultiResizeMode"] = ed->viewportPanel->multiResizeMode == ViewportPanel::MultiResizeMode::GroupScale
        ? "GroupScale" : "Individual";
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

class EditorInteractionTracker {
public:
    explicit EditorInteractionTracker(GLFWwindow* window)
        : m_previousKeyState(GLFW_KEY_LAST + 1, GLFW_RELEASE)
    {
        glfwGetCursorPos(window, &m_previousCursorX, &m_previousCursorY);
        glfwGetFramebufferSize(
            window, &m_previousFramebufferWidth, &m_previousFramebufferHeight);
    }

    bool detect(GLFWwindow* window, bool windowInactive)
    {
        bool interaction = false;
        if (!windowInactive) {
            for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
                const unsigned char keyState =
                    glfwGetKey(window, key) == GLFW_PRESS ? GLFW_PRESS : GLFW_RELEASE;
                if (keyState != m_previousKeyState[key]) {
                    interaction = true;
                }
                m_previousKeyState[key] = keyState;
            }

            for (int button = 0; button < 3; ++button) {
                const unsigned char buttonState =
                    glfwGetMouseButton(window, button) == GLFW_PRESS ? GLFW_PRESS : GLFW_RELEASE;
                if (buttonState != m_previousMouseState[button]) {
                    interaction = true;
                }
                m_previousMouseState[button] = buttonState;
            }

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(window, &cursorX, &cursorY);
            if (cursorX != m_previousCursorX || cursorY != m_previousCursorY) {
                interaction = true;
            }
            m_previousCursorX = cursorX;
            m_previousCursorY = cursorY;

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth != m_previousFramebufferWidth ||
                framebufferHeight != m_previousFramebufferHeight) {
                interaction = true;
            }
            m_previousFramebufferWidth = framebufferWidth;
            m_previousFramebufferHeight = framebufferHeight;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (!windowInactive) {
            interaction = interaction || io.MouseDelta.x != 0.0f ||
                          io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f ||
                          io.MouseWheelH != 0.0f || io.InputQueueCharacters.Size > 0;
        }
        return interaction;
    }

private:
    std::vector<unsigned char> m_previousKeyState;
    std::array<unsigned char, 3> m_previousMouseState{
        GLFW_RELEASE, GLFW_RELEASE, GLFW_RELEASE
    };
    double m_previousCursorX = 0.0;
    double m_previousCursorY = 0.0;
    int m_previousFramebufferWidth = 0;
    int m_previousFramebufferHeight = 0;
};

class StudioApplication {
public:
    explicit StudioApplication(const LaunchOptions& options)
        : m_options(options), m_settings(options.settingsPath)
    {
    }

    int run()
    {
        if (!initialize()) {
            shutdown();
            return -1;
        }

        runLoop();
        shutdown();
        return 0;
    }

private:
    struct FrameState {
        double now = 0.0;
        float deltaTime = 0.0f;
        bool windowInactive = false;
        bool isPlaying = false;
        bool isPaused = false;
        bool navMeshBusy = false;
        bool runtimeFrameOk = true;
        bool playTransitionAccepted = true;
    };

    bool initialize()
    {
        m_window = createWindow();
        if (!m_window) {
            return false;
        }

        m_renderer = std::make_unique<Renderer>();
        m_audio = std::make_unique<AudioService>();
        m_system = std::make_shared<System>();
        m_luau = std::make_unique<LuauEngine>();
        m_user = std::make_shared<User>(std::make_unique<GLFWInputBackend>(m_window));
        m_user->setControlMode(User::ControlMode::Free);
        m_renderer->init(m_window);
        if (!m_audio->initialize()) {
            RCBN_ERROR("AudioService initialization failed.");
            return false;
        }

        Physics::s_touchCallback = [this](BaseCube* a, BaseCube* b) {
            m_luau->onTouched(a, b);
        };
        Physics::s_touchEndCallback = [this](BaseCube* a, BaseCube* b) {
            m_luau->onTouchEnded(a, b);
        };

        return initializeSceneAndEditor();
    }

    bool initializeSceneAndEditor()
    {
        const auto lastScenePath = m_settings.loadLastScenePath();
        updateWindowTitle(m_window, m_options.initialScenePath);
        auto bound = SceneRuntime::loadAndBind(
            m_options.initialScenePath, m_system, m_user, *m_luau, m_window);
        const auto metadata = bound.metadata;
        if (!installScene(std::move(bound), false)) {
            return false;
        }
        migrateLegacyLighting(m_system, m_workspace, m_workspaces);

        m_renderer->m_onButtonActivated = [this](GuiButton* button) {
            m_luau->onGuiButtonActivated(button);
        };

        auto editor = std::make_unique<EditorManager>(
            m_workspace.get(), m_user.get(), m_system.get(), m_options.autosaveRoot,
            m_renderer->codeEditorFont(), m_luau->runtimeFileSystem());
        m_editor = editor.get();
        m_editor->setSceneMetadata(metadata);
        m_editor->engineExePath = m_options.executablePath.string();
        m_editor->scenePath = m_options.initialScenePath;
        m_editor->initializeAutosaveRecovery();
        m_settings.loadPanelVisibility(m_editor);
        m_settings.loadEditorPreferences(m_editor, m_user.get(), m_window);
        m_editor->welcomePanel->lastScenePath = lastScenePath;

        m_editor->hierarchyPanel->onSwitchWorkspace = [this](Workspace* workspace) {
            switchWorkspace(workspace);
        };
        m_editor->hierarchyPanel->onOpenSecondaryViewport = [this](Workspace* workspace) {
            m_editor->openSecondaryViewport(workspace);
        };
        m_renderer->editor = std::move(editor);
        RCBN_LOG("Editor initialized.");

        m_lastFrame = glfwGetTime();
        m_lastTitlePath = m_editor->scenePath;
        m_lastEditorInteraction = glfwGetTime();
        m_interactionTracker.emplace(m_window);
        return true;
    }

    bool installScene(SceneRuntime::Bound bound, bool markDirty)
    {
        if (!bound.workspace) {
            RCBN_ERROR("Scene binding did not provide a Workspace.");
            return false;
        }

        m_workspace = std::move(bound.workspace);
        m_workspaces = std::move(bound.workspaces);
        m_luau->setRuntimeFileSystem(std::make_shared<RuntimeFileSystem>(
            m_system->EnableExternalFileAccess));
        if (m_editor) {
            m_editor->setRuntimeFileSystem(m_luau->runtimeFileSystem());
            m_editor->setSceneMetadata(bound.metadata);
            m_editor->setWorkspace(m_workspace.get());
            if (markDirty) {
                m_editor->markDirty();
            }
        }
        m_workspace->initPhysics();
        return true;
    }

    void clearSceneForReplacement()
    {
        releaseEditorSceneReferences();
        releaseSceneRuntime();
    }

    void releaseEditorSceneReferences()
    {
        // Undo や Clipboard が旧 Workspace の Instance を保持しないよう先に解放する。
        m_editor->hierarchyPanel->selectedInstance = nullptr;
        m_editor->m_history.clear();
        m_editor->clearClipboard();
        m_editor->animationPanel->endEditSession();
    }

    void releaseSceneRuntime()
    {
        m_workspaces = SceneRuntime::collectWorkspaces(m_system);
        m_luau->cancelAllTasks();
        SceneRuntime::releaseTerrainStreamers(m_workspaces);
        for (auto& workspace : m_workspaces) {
            if (workspace && workspace->getPhysicsEngine()) {
                workspace->getPhysicsEngine()->clearCubes();
            }
        }
    }

    bool loadPendingScene()
    {
        if (!m_editor || !m_editor->pendingScene.active || !m_editor->isEditMode()) {
            return true;
        }

        const auto request = m_editor->pendingScene;
        const bool isNewScene = request.kind == EditorManager::PendingSceneKind::New;
        const std::string path = isNewScene ? std::string() : request.sourcePath;
        auto staged = SceneRuntime::stageSceneLoad(path, m_system, m_user);
        if (!staged) {
            m_editor->showSceneLoadError(
                staged.message.empty() ? "Scene could not be loaded" : staged.message);
            m_editor->pendingScene = {};
            return false;
        }

        m_editor->pendingScene = {};
        m_editor->endAutosaveSessionNormally();
        clearSceneForReplacement();
        if (!installScene(SceneRuntime::commitAndBind(
                std::move(staged), m_system, m_user, *m_luau, m_window), false)) {
            return false;
        }
        m_editor->scenePath = request.logicalPath;
        m_editor->beginAutosaveSession();
        if (request.markDirty) {
            m_editor->markDirty();
        }
        m_editor->evaluateSceneMigration();
        SceneRuntime::applyAppIcon(m_window, m_system.get());
        return true;
    }

    void switchWorkspace(Workspace* workspace)
    {
        if (!workspace || workspace == m_workspace.get()) {
            return;
        }
        m_workspaces = SceneRuntime::collectWorkspaces(m_system);
        const bool playing = SystemState::get().isPlaying;
        if (playing && m_user->character && !m_user->moveCharacterToWorkspace(*workspace)) {
            return;
        }
        bindWorkspace(*workspace, playing, false);
    }

    void restorePlaySnapshot()
    {
        m_audio->stopAllSounds();
        m_user->despawnCharacter();
        clearSceneForReplacement();
        installScene(
            SceneRuntime::loadAndBind(SNAPSHOT_PATH, m_system, m_user, *m_luau, m_window),
            m_snapshotDirty);
        m_editor->setSceneMetadata(m_originalSceneMetadata);
    }

    void runLoop()
    {
        while (tickFrame() != FrameResult::Exit) {
        }
        std::cout << "[DEBUG] Main loop ended.\n";
    }

    enum class FrameResult { Continue, Skip, Exit };

    FrameResult tickFrame()
    {
        FrameState frame;
        if (!beginFrame(frame)) {
            return FrameResult::Exit;
        }

        updatePlayTransition(frame);
        if (m_editor->isEditMode()) {
            m_editor->updateAutosave();
        }
        if (!loadPendingScene()) {
            return FrameResult::Skip;
        }
        refreshSceneTitle();
        updateSimulation(frame);

        if (updateInput(frame)) {
            return FrameResult::Exit;
        }
        updateHumanoids(frame);
        if (!cycleWorkspace(frame)) {
            return FrameResult::Skip;
        }
        updateWorkspaceEffects(frame);
        renderFrame();
        recordEditorInteraction(frame);
        return FrameResult::Continue;
    }

    bool beginFrame(FrameState& frame)
    {
        frame.windowInactive = glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_FALSE ||
            glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE;
        if (!m_wasPlaying && !PathfindingService::IsBuildActive() &&
            (m_energySavingMode || frame.windowInactive)) {
            // Wake on input, resize or close, or refresh once at the idle rate.
            glfwWaitEventsTimeout(1.0 / 15.0);
        }

        // don't delete just comment-out it
        if (glfwGetKey(m_window, GLFW_KEY_F12) == GLFW_PRESS) {
            std::cout << "F12 emergency stop DD:\n";
            std::abort();
        }
        if (glfwWindowShouldClose(m_window) && checkExit()) {
            return false;
        }

        frame.now = glfwGetTime();
        frame.deltaTime = static_cast<float>(std::max(0.0, frame.now - m_lastFrame));
        m_lastFrame = frame.now;
        pollNetworkClientCleanup(frame.now);

        auto& state = SystemState::get();
        state.deltaTime = frame.deltaTime;
        // 非同期 FindPath の完了はシミュレーション停止中も処理する。
        m_luau->pollPathfindingRequests();
        frame.navMeshBusy = PathfindingService::IsBuildActive();
        frame.isPlaying = state.isPlaying = !m_editor->isEditMode();
        frame.isPaused = state.isPaused = m_editor->isPauseMode();
        state.inputState = frame.isPlaying ? InputState::Gameplay : InputState::Editor;
        return true;
    }

    bool checkExit()
    {
        if (m_editor->isDirty()) {
            m_editor->requestSaveDialog(m_window);
        } else if (m_editor->hasDirtyCodeEditors()) {
            m_editor->requestCodeEditorSaveDialog(m_window);
        } else {
            return true;
        }
        glfwSetWindowShouldClose(m_window, GLFW_FALSE);
        return false;
    }

    void refreshSceneTitle()
    {
        const bool dirty = m_editor->isDirty();
        const auto& path = m_editor->scenePath;
        if (dirty == m_lastDirty && path == m_lastTitlePath) {
            return;
        }
        updateWindowTitle(m_window, path, dirty);
        if (path != m_lastTitlePath && !path.empty()) {
            m_settings.saveLastScenePath(path);
        }
        m_lastDirty = dirty;
        m_lastTitlePath = path;
    }

    void updateSimulation(FrameState& frame)
    {
        frame.runtimeFrameOk = frame.playTransitionAccepted;
        if (frame.isPlaying && frame.runtimeFrameOk) {
            syncCharacterWorkspace();
        }
        updateLocalServer(frame);
        updateRuntime(frame);
        if (!frame.isPlaying && m_workspace) {
            // Play 中は Physics が Model の原点を同期する。
            Model::syncPivotsToPrimaryCube(m_workspace->getRenderModels());
        }
        frame.navMeshBusy = PathfindingService::IsBuildActive();
    }

    bool updateInput(const FrameState& frame)
    {
        auto& state = SystemState::get();
        const auto* viewport = GetFocusedViewport();
        state.viewportFocused = !frame.windowInactive && viewport &&
            viewport == m_editor->viewportPanel.get();
        state.viewportHovered = m_editor->viewportPanel && m_editor->viewportPanel->isHoveringViewport;
        if (!frame.navMeshBusy) {
            FrameProfiler::Scope processInput("main.processInput");
            m_user->processInput(
                m_workspace->getPhysicsEngine(), frame.deltaTime,
                state.viewportFocused, state.viewportHovered,
                state.inputState == InputState::Gameplay, ImGui::GetIO().WantTextInput);
        }
        return m_user->consumeExitRequest() && checkExit();
    }

    void updateHumanoids(const FrameState& frame)
    {
        // processInput 内のアニメーションより後に、NPC を含む全 Humanoid を更新する。
        if (frame.isPlaying && frame.runtimeFrameOk && !frame.isPaused && !frame.navMeshBusy) {
            FrameProfiler::Scope humanoids("main.humanoids");
            Humanoid::updateAll(m_workspace->getRenderHumanoids(),
                frame.deltaTime, m_workspace->getPhysicsEngine());
        }
    }

    void syncCharacterWorkspace()
    {
        auto* workspace = m_user->getCharacterWorkspace();
        if (workspace && workspace != m_workspace.get()) {
            bindWorkspace(*workspace, true, true);
        }
    }

    bool cycleWorkspace(const FrameState& frame)
    {
        if (frame.navMeshBusy || !frame.runtimeFrameOk ||
            !m_user->consumeWorkspaceSwitchRequest() || !frame.isPlaying) {
            return true;
        }

        m_workspaces = SceneRuntime::collectWorkspaces(m_system);
        if (m_workspaces.size() <= 1) {
            return true;
        }

        auto next = std::ranges::find(m_workspaces, m_workspace);
        if (next == m_workspaces.end() || ++next == m_workspaces.end()) {
            next = m_workspaces.begin();
        }
        if (*next != m_workspace) {
            if (m_user->character && !m_user->moveCharacterToWorkspace(**next)) {
                return false;
            }
            bindWorkspace(**next, true, true);
        }
        return true;
    }

    void bindWorkspace(Workspace& workspace, bool initializePhysics, bool updateReplication)
    {
        m_workspace = std::static_pointer_cast<Workspace>(workspace.shared_from_this());
        if (initializePhysics && !m_workspace->getPhysicsEngine()) {
            m_workspace->initPhysics();
        }
        if (updateReplication && m_replication) {
            m_replication->setWorkspace(m_workspace);
        }
        m_luau->setGlobalInstance("workspace", m_workspace);
        m_luau->setWorkspace(m_workspace);
        m_editor->setWorkspace(m_workspace.get());
    }

    void updateWorkspaceEffects(const FrameState& frame)
    {
        if (frame.navMeshBusy) {
            return;
        }
        {
            FrameProfiler::Scope terrains("main.terrains");
            Vector3 center = m_user->cpos;
            if (m_user->humanoid) {
                if (auto root = m_user->humanoid->getRootPart()) {
                    center = root->getWorldCFrame().Position;
                }
            }
            SceneRuntime::updateTerrains(m_workspace.get(), center);
        }
        {
            // ParticleEmitter より先に風と発生源位置を反映する。
            FrameProfiler::Scope weather("main.weather");
            Weather::updateAll(m_workspace.get(), m_workspace->getRenderWeathers(),
                frame.deltaTime, m_user->cpos);
        }
        {
            FrameProfiler::Scope particles("main.particles");
            ParticleEmitter::updateAll(m_workspace->getRenderParticleEmitters(), frame.deltaTime);
        }
    }

    void recordEditorInteraction(const FrameState& frame)
    {
        // A held key does not continuously wake the editor from energy-saving mode.
        const bool interaction = m_interactionTracker->detect(m_window, frame.windowInactive);
        const double now = glfwGetTime();
        if (interaction || frame.isPlaying || frame.navMeshBusy) {
            m_lastEditorInteraction = now;
        }
        // Idle throttling for a focused editor remains disabled.
        // m_energySavingMode = !frame.isPlaying &&
        //     (frame.windowInactive || now - m_lastEditorInteraction >= 2.0);
    }


    void updateRuntime(FrameState& frame)
    {
        if (!frame.isPlaying || !frame.runtimeFrameOk || frame.isPaused ||
            frame.navMeshBusy) {
            return;
        }

        FrameProfiler::get().beginSection("luau");
        m_luau->resetFrameSafetyCounters();
        for (auto& [name, child] : m_system->getChildren()) {
            if (!child->IsA("Workspace")) {
                continue;
            }

            auto* workspace = static_cast<Workspace*>(child.get());
            if (!workspace->getPhysicsEngine()) {
                workspace->initPhysics();
            }
            m_luau->executeWorkspaceScripts(*workspace);
            if (PathfindingService::IsBuildActive()) {
                frame.navMeshBusy = true;
                break;
            }

            FrameProfiler::get().endSection("luau");
            FrameProfiler::get().beginSection("physics");
            workspace->getPhysicsEngine()->update(*workspace, frame.deltaTime);
            FrameProfiler::get().endSection("physics");
            FrameProfiler::get().beginSection("luau");
        }

        if (!frame.navMeshBusy) {
            m_luau->executeSystemScripts();
            if (!PathfindingService::IsBuildActive()) {
                m_luau->fireHeartbeat(frame.deltaTime);
                m_luau->update(frame.deltaTime);
            } else {
                frame.navMeshBusy = true;
            }
        }
        syncCharacterWorkspace();
        FrameProfiler::get().endSection("luau");

        if (m_luau->consumeSafetyHaltRequest()) {
            // Stopボタン(EditorManager.cpp)と同じ状態遷移
            m_editor->mode = EditorMode::Edit;
            m_user->setControlMode(User::ControlMode::Free);
            RCBN_LOG("[INFO] Stopped due to safety limit breach. Switched to Free Camera mode.");
        }
    }


    struct ManagedClient {
        std::unique_ptr<IChildProcess> process;
        std::filesystem::path logPath;
        bool exitReported = false;
    };

    static constexpr const char* SNAPSHOT_PATH = "assets/scenes/_snapshot.rcbn";

    void updatePlayTransition(FrameState& frame)
    {
        if (frame.isPlaying && !m_wasPlaying) {
            frame.playTransitionAccepted = startPlay();
        } else if (!frame.isPlaying && m_wasPlaying) {
            stopPlay();
        }
        m_wasPlaying = frame.isPlaying && frame.playTransitionAccepted;
    }

    void returnToEdit()
    {
        m_editor->mode = EditorMode::Edit;
        m_user->setControlMode(User::ControlMode::Free);
        SystemState::get().isPlaying = false;
        SystemState::get().inputState = InputState::Editor;
    }

    bool startPlay()
    {
        const auto mode = m_editor->activePlayMode();
        const auto spawnPosition = mode == EditorPlayMode::PlayHere
            ? std::optional<Vector3>(m_user->getCameraCFrame().Position)
            : std::nullopt;
        if (mode == EditorPlayMode::LocalServer && !m_system->UseNetwork) {
            m_editor->showLocalServerNetworkRequiredError();
            returnToEdit();
            return false;
        }

        savePlaySnapshot();
        if (mode == EditorPlayMode::LocalServer && !startLocalServer()) {
            beginNetworkClientCleanup();
            clearEditorNetwork();
            restoreObserverBinding();
            restorePlaySnapshot();
            returnToEdit();
            return false;
        }

        // LocalServer の NetworkRole では LocalScript が除外される。
        for (auto& [name, child] : m_system->getChildren()) {
            if (child->IsA("Workspace")) {
                m_luau->executeWorkspaceScripts(*static_cast<Workspace*>(child.get()));
            }
        }
        m_luau->executeSystemScripts();

        if (mode != EditorPlayMode::LocalServer) {
            // CharacterAdded からも Play Here の初期位置を参照できるようにする。
            if (m_system->AutoSpawnPlayerCharacter) {
                m_user->spawnCharacter(m_system.get(), m_workspace.get(), spawnPosition);
            }
            m_audio->playAutoPlaySounds();
            if (m_user->character) {
                m_workspace->addChild(m_user->character);
            }
        }
        return true;
    }

    void savePlaySnapshot()
    {
        // Play 開始時に Inventory の実体から Slots を再構築し、System Script も再実行可能にする。
        m_user->syncToolsFromInventory();
        m_luau->resetSystemScripts();
        m_snapshotDirty = m_editor->isDirty();
        m_originalSceneMetadata = m_editor->sceneMetadata();
        // 編集中のプレビュー姿勢をスナップショットに焼き込まない。
        if (m_editor->animationPanel) {
            m_editor->animationPanel->endEditSession();
        }
        m_editor->flushAutosaveRecovery();
        SceneLoader::saveSceneResult(m_system.get(), SNAPSHOT_PATH, m_editor->sceneMetadata());
        SceneLoader::resolveConstraintRefs(m_system.get());

        for (auto& [name, child] : m_system->getChildren()) {
            if (child->IsA("Workspace")) {
                auto& workspace = static_cast<Workspace&>(*child);
                if (!workspace.getPhysicsEngine()) {
                    workspace.initPhysics();
                }
            }
        }
    }

    void stopPlay()
    {
        if (m_editor->activePlayMode() == EditorPlayMode::LocalServer) {
            beginNetworkClientCleanup();
            clearEditorNetwork();
        }
        restorePlaySnapshot();
    }

    bool startLocalServer()
    {
        std::error_code error;
        const auto studioPath = std::filesystem::absolute(m_options.executablePath, error);
        if (error || studioPath.empty()) {
            m_editor->showPlayStartError("Failed to resolve the editor executable path.");
            return false;
        }
#ifdef _WIN32
        const auto enginePath = studioPath.parent_path() / "RecubinEngine.exe";
#else
        const auto enginePath = studioPath.parent_path() / "RecubinEngine";
#endif
        if (!std::filesystem::is_regular_file(enginePath, error) || error) {
            m_editor->showPlayStartError("RecubinEngine was not found next to the editor executable.");
            return false;
        }
        const auto workingDirectory = std::filesystem::current_path(error);
        if (error) {
            m_editor->showPlayStartError("Failed to resolve the project working directory.");
            return false;
        }
        const auto sessionId = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto logDirectory = std::filesystem::temp_directory_path(error) /
            "RecubinStudio" / ("local-server-" + std::to_string(sessionId));
        if (!error) {
            std::filesystem::create_directories(logDirectory, error);
        }
        if (error) {
            m_editor->showPlayStartError("Failed to create the client log directory.");
            return false;
        }
        auto& network = NetworkManager::get();
        if (!network.startHost(0, false)) {
            m_editor->showPlayStartError("Failed to start the local direct server.");
            return false;
        }

        bindLocalServer();
        const auto port = network.getListenPort();
        const int clientCount = std::clamp(m_editor->networkClientCount(), 1, 8);
        m_editor->setNetworkClientStatus(0, clientCount);
        for (int index = 1; index <= clientCount; ++index) {
            const std::string clientName = "Client " + std::to_string(index);
            const auto logPath = logDirectory / ("client-" + std::to_string(index) + ".log");
            ChildProcessLaunchOptions options;
            options.executable = enginePath.string();
            options.arguments = {
                "--scene", SNAPSHOT_PATH,
                "--direct-connect", "127.0.0.1:" + std::to_string(port),
                "--listen-port", "0",
                "--window-title", clientName,
                "--editor-test"
            };
            options.workingDirectory = workingDirectory.string();
            options.outputLogPath = logPath.string();
            auto process = getPlatform().launchChildProcess(options);
            if (!process) {
                m_editor->showPlayStartError("Failed to launch " + clientName + ".");
                return false;
            }
            m_networkClients.push_back({std::move(process), logPath});
        }
        return true;
    }

    void bindLocalServer()
    {
        m_user->setControlMode(User::ControlMode::Free);
        m_user->despawnCharacter();
        if (auto parent = m_user->Parent.lock()) {
            parent->removeChild(m_user->Name);
        }
        m_luau->clearGlobalInstance("User");

        auto& network = NetworkManager::get();
        if (auto it = m_system->children.find("ChatService"); it != m_system->children.end()) {
            m_chat = std::dynamic_pointer_cast<ChatService>(it->second);
        }
        if (m_chat) {
            m_chat->onSendRequested = [](const std::string& text) {
                NetworkManager::get().sendChatMessage(text);
            };
            m_renderer->setChatService(m_chat);
        }
        m_replication = std::make_unique<ReplicationManager>(m_workspace, m_user, m_system.get());
        network.onGameMessage = [this](uint8_t type, const uint8_t* payload, size_t len, PeerId senderId) {
            if (m_replication) {
                m_replication->onGameMessage(type, payload, len, senderId);
            }
        };
        network.onChatMessage = [this](PeerId senderId, const std::string& text) {
            RCBN_LOG("[Chat][Peer " << senderId << "] " << text);
            if (m_chat) {
                m_chat->receiveMessage(senderId, text);
                m_luau->fireChatMessage(m_chat.get(), senderId, text);
            }
        };
        network.onRoleChanged = [this](NetworkRole oldRole, NetworkRole newRole) {
            RCBN_LOG("NetworkManager: role changed " << NetworkManager::roleToString(oldRole)
                     << " -> " << NetworkManager::roleToString(newRole));
            m_luau->fireNetworkRoleChanged(oldRole, newRole);
            if (m_replication) {
                m_replication->onNetworkRoleChanged(oldRole, newRole);
            }
        };
    }

    void restoreObserverBinding()
    {
        for (auto& [name, child] : m_system->getChildren()) {
            if (child && child->IsA("Users")) {
                if (m_user->Parent.expired()) {
                    child->addChild(m_user);
                }
                break;
            }
        }
        m_luau->setGlobalInstance("User", m_user);
        m_user->setControlMode(User::ControlMode::Free);
    }

    void updateLocalServer(FrameState& frame)
    {
        if (!frame.isPlaying || !frame.playTransitionAccepted ||
            m_editor->activePlayMode() != EditorPlayMode::LocalServer) {
            return;
        }

        auto& network = NetworkManager::get();
        m_user->setControlMode(User::ControlMode::Free);
        network.update(frame.deltaTime);
        const auto& roster = network.getRoster();
        const auto connectedPlayers = std::ranges::count_if(roster, [](const PeerInfo& peer) {
            return peer.isPlayer;
        });
        m_editor->setNetworkClientStatus(
            static_cast<int>(connectedPlayers), std::clamp(m_editor->networkClientCount(), 1, 8));
        for (auto& client : m_networkClients) {
            if (client.exitReported || client.process->isRunning()) {
                continue;
            }
            const auto exitCode = client.process->exitCode();
            RCBN_LOG("[Network Test] Client exited"
                     << (exitCode ? " with code " + std::to_string(*exitCode) : std::string())
                     << ". Log: " << client.logPath.string());
            client.exitReported = true;
        }

        if (!network.isActive() || network.getConnectionState() == ConnectionState::Failed) {
            m_editor->showPlayStartError(std::string("Local server failed: ") +
                NetworkManager::connectionErrorToString(network.getConnectionError()));
            returnToEdit();
            frame.runtimeFrameOk = false;
            return;
        }

        if (!frame.navMeshBusy && m_replication) {
            m_replication->update(frame.deltaTime, m_workspace->getPhysicsEngine());
            if (m_replication->hasFatalIdentityError()) {
                m_editor->showPlayStartError("Local server replication failed.");
                returnToEdit();
                frame.runtimeFrameOk = false;
            }
        }
    }

    void clearEditorNetwork()
    {
        auto& network = NetworkManager::get();
        network.onRoleChanged = nullptr;
        network.onGameMessage = nullptr;
        network.onChatMessage = nullptr;
        if (m_chat) {
            m_chat->onSendRequested = nullptr;
        }
        m_renderer->setChatService(nullptr);
        m_chat.reset();
        m_replication.reset();
        network.shutdown();
        m_editor->setNetworkClientStatus(0, 0);
    }

    void requestNetworkClientsClose()
    {
        for (auto& client : m_networkClients) {
            if (client.process->isRunning()) {
                client.process->requestClose();
            }
        }
    }

    void beginNetworkClientCleanup()
    {
        if (m_networkClients.empty()) {
            return;
        }
        requestNetworkClientsClose();
        m_networkCleanupDeadline = glfwGetTime() + 2.0;
        m_editor->setExternalPlayCleanup(true);
    }

    void pollNetworkClientCleanup(double now)
    {
        if (!m_networkCleanupDeadline) {
            return;
        }
        bool allStopped = true;
        for (auto& client : m_networkClients) {
            if (client.process->isRunning()) {
                allStopped = false;
                if (now >= *m_networkCleanupDeadline) {
                    client.process->terminate();
                }
            }
        }
        if (allStopped) {
            finishNetworkClientCleanup();
        }
    }

    void finishNetworkClientCleanup()
    {
        m_networkClients.clear();
        m_networkCleanupDeadline.reset();
        m_editor->setExternalPlayCleanup(false);
    }

    void stopLocalServerForExit()
    {
        requestNetworkClientsClose();
        // 子プロセスに通常終了の猶予を与えてから、残っているものだけを終了する。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        const auto clientRunning = [](const ManagedClient& client) {
            return client.process->isRunning();
        };
        while (std::chrono::steady_clock::now() < deadline &&
               std::ranges::any_of(m_networkClients, clientRunning)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (auto& client : m_networkClients) {
            if (client.process->isRunning()) {
                client.process->terminate();
            }
        }
        finishNetworkClientCleanup();
        clearEditorNetwork();
    }


    void renderFrame()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_renderer->render(*m_user, m_window, *m_workspace);
        m_audio->updateSounds(m_user->cpos, m_user->right);
        FrameProfiler::get().endFrame();
    }

    void shutdown()
    {
        if (!m_window) {
            return;
        }
        RCBN_LOG("Shutting down...");
        if (m_editor) {
            m_settings.savePanelVisibility(m_editor);
            m_settings.saveEditorPreferences(m_editor, m_user.get());
            stopLocalServerForExit();
            m_editor->endAutosaveSessionNormally();
            releaseEditorSceneReferences();
        }

        // Workspace の物理と GL が有効な間に Editor の参照をすべて解放する。
        m_renderer->editor.reset();
        m_editor = nullptr;
        m_renderer->m_onButtonActivated = nullptr;

        if (m_luau && m_system) {
            releaseSceneRuntime();
            for (auto& workspace : m_workspaces) {
                if (workspace && workspace->getPhysicsEngine()) {
                    workspace->setPhysicsEngine(nullptr);
                }
            }
            for (const auto& workspace : m_workspaces) {
                if (workspace) {
                    m_system->removeChild(workspace->Name);
                }
            }
        }
        Physics::s_touchCallback = nullptr;
        Physics::s_touchEndCallback = nullptr;
        m_workspace.reset();
        m_workspaces.clear();
        m_luau.reset();
        m_user.reset();
        m_system.reset();
        m_audio.reset();
        m_renderer.reset();

        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }

    LaunchOptions m_options;
    EditorSettings m_settings;
    bool m_wasPlaying = false;
    bool m_snapshotDirty = false;
    SceneLoader::SceneDocumentMetadata m_originalSceneMetadata;
    std::vector<ManagedClient> m_networkClients;
    std::optional<double> m_networkCleanupDeadline;
    std::unique_ptr<ReplicationManager> m_replication;
    std::shared_ptr<ChatService> m_chat;
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<AudioService> m_audio;
    std::shared_ptr<System> m_system;
    std::unique_ptr<LuauEngine> m_luau;
    std::shared_ptr<User> m_user;
    std::shared_ptr<Workspace> m_workspace;
    std::vector<std::shared_ptr<Workspace>> m_workspaces;
    EditorManager* m_editor = nullptr; // Renderer が所有する。
    double m_lastFrame = 0.0;
    std::string m_lastTitlePath;
    bool m_lastDirty = false;
    std::optional<EditorInteractionTracker> m_interactionTracker;
    double m_lastEditorInteraction = 0.0;
    bool m_energySavingMode = false;
};

} // namespace

int runStudio(const LaunchOptions& options)
{
    return StudioApplication(options).run();
}

} // namespace StudioMain
