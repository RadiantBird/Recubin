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

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>
#include <Core/AudioService.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>

#include <Editor/EditorManager.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Core/SystemState.hpp>
#include <include/imgui/imgui.h>

#include <Util/Logger.hpp>
#include <Util/FileDialog.hpp>

#include <iostream>
#include <algorithm>
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

#define ENABLE_BETA 1 // enable beta features(such as terrain)
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

// AppImage インスタンスを Root から探してウィンドウアイコンを設定する
void applyAppIcon(GLFWwindow* window, Instance* root) {
    if (!window || !root) return;
    for (auto& [name, child] : root->children) {
        if (child->getClassName() == "AppImage") {
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

// エディター設定（前回開いていたシーンパスなど）の保存先
static const std::string kEditorSettingsPath = "editor_settings.yaml";

// 前回開いていたシーンパスを読み込む。記録が無い/壊れている場合は空文字列を返す
static std::string loadLastScenePath() {
    try {
        YAML::Node root = YAML::LoadFile(kEditorSettingsPath);
        if (root["LastScenePath"]) {
            return root["LastScenePath"].as<std::string>();
        }
    } catch (...) {
        // 設定ファイルが無い場合は何もしない
    }
    return "";
}

// 次回起動時に自動で開けるよう、現在のシーンパスを記録する
static void saveLastScenePath(const std::string& path) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "LastScenePath" << YAML::Value << path;
    out << YAML::EndMap;

    std::ofstream file(kEditorSettingsPath);
    if (file) file << out.c_str();
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
//  main
// ===================================================
int main(int argc, char* argv[]) {
    // コンソールの出力/入力コードページをUTF-8にする
    // (Windows日本語版等では既定のANSIコードページのままだと、UTF-8で書かれた
    //  ログやLuauのprint出力が文字化けする)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "Hello world!\n"
              << "Recubin Studio v0.990\n";
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
    auto user         = std::make_shared<User>(std::make_unique<GLFWInputBackend>(window));
    user->controlMode = User::ControlMode::Free; // エディターではフリーモードから開始(パッケージされたゲームランタイムはCharacterモードから開始)
    user->initializeInventory();  // Inventory を初期化

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
    SceneLoader::registerSingleton("User", user);

    // 前回開いていたシーンを記憶しておき、次回起動時に自動で開く。
    // 記録が無い/ファイルが見つからない場合はダイアログで選択させる(キャンセルなら正常終了)
    std::string scenePath = loadLastScenePath();
    if (scenePath.empty() || !std::filesystem::exists(scenePath)) {
        scenePath = browseFile(L"Scene (*.yaml;*.yml)", L"*.yaml;*.yml");
        if (scenePath.empty()) {
            std::cout << "[INFO] シーンが選択されなかったため終了します。\n";
            glfwTerminate();
            return 0;
        }
    }
    saveLastScenePath(scenePath);
    updateWindowTitle(window, scenePath);

    SceneLoader::loadScene(scenePath);
    SceneLoader::clearSingletons();

    workspaces = collectWorkspaces(system);
    if (workspaces.empty()) {
        workspace = std::make_shared<Workspace>();
        auto lighting = std::make_shared<Lighting>();
        lighting->Name = "Lighting";
        system->addChild(workspace);
        system->addChild(user);
        workspace->addChild(lighting);
        workspaces = collectWorkspaces(system);
    }
    workspace = workspaces.front();

    // ユーザーがシステムに含まれていない場合は追加
    auto it = system->children.find("User");
    if (it == system->children.end()) {
        system->addChild(user);
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

    applyAppIcon(window, system.get());

    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setGlobalInstance("system", system);
    luauEngine->setGlobalInstance("User", user);
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
    ed->scenePath     = scenePath; // 起動時に決定したシーンパスを反映

    // Workspace 切り替えコールバックを設定
    ed->hierarchyPanel->onSwitchWorkspace = [&](Workspace* ws) {
        auto wsSp = std::static_pointer_cast<Workspace>(ws->shared_from_this());
        workspaces = collectWorkspaces(system);
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

    auto initNewScene = [&](const std::string& path, bool isDirty) {
        SceneLoader::registerSingleton("System", system);
        SceneLoader::registerSingleton("User", user);
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
        // Terrainを新Workspaceにリセット（次のupdate()で再初期化される）
    };

    while (true) {
        if (glfwWindowShouldClose(window)) {
            if (checkExit(ed, *window)) {
                break;
            }
        }
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        SystemState& state = SystemState::get();
        state.isPlaying  = ed && !ed->isEditMode();
        state.isPaused   = ed &&  ed->isPauseMode();
        state.inputState = state.isPlaying ? InputState::Gameplay : InputState::Editor;

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
            user->spawnCharacter(system.get());
            audioService->playAutoPlaySounds();
            if (user->character) workspace->addChild(user->character);
        }
        if (!isPlaying && wasPlaying) {
            audioService->stopAllSounds();
            user->despawnCharacter();
            // 全Workspaceのクリア（ownedPhysics デストラクタで自動解放）
            // Terrainは次回のload時に再構築される
            workspaces = collectWorkspaces(system);
            resetTerrainStreamers(workspaces); // 物理が生きているうちにTerrainを解放
            clearWorkspacePhysics(workspaces);
            removeWorkspacesFromSystem(system, workspaces);

            initNewScene(snapshotPath, snapshotDirty);
        }
        wasPlaying = isPlaying;

        // ---- Load ボタンによるシーンリロード ----
        if (ed && !ed->pendingLoadPath.empty() && ed->isEditMode()) {
            std::string loadPath = ed->pendingLoadPath;
            ed->pendingLoadPath.clear();

            // Terrainは次回のload時に再構築される
            workspaces = collectWorkspaces(system);
            resetTerrainStreamers(workspaces); // 物理が生きているうちにTerrainを解放
            clearWorkspacePhysics(workspaces);
            removeWorkspacesFromSystem(system, workspaces);

            initNewScene(loadPath, false);
            ed->scenePath = loadPath;
            saveLastScenePath(loadPath);
            updateWindowTitle(window, loadPath, ed->isDirty());
            applyAppIcon(window, system.get());
        }

        // ---- 未保存状態が変化したらタイトルバーの "*" を更新する ----
        {
            static bool s_lastDirty = false;
            bool dirty = ed && ed->isDirty();
            if (dirty != s_lastDirty) {
                updateWindowTitle(window, ed ? ed->scenePath : scenePath, dirty);
                s_lastDirty = dirty;
            }
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
        user->processInput(workspace->getPhysicsEngine(), deltaTime,
                            state.viewportFocused, state.viewportZoomEnabled,
                            state.inputState == InputState::Gameplay,
                            ImGui::GetIO().WantTextInput);
        if (user->consumeExitRequest()) {
            if (checkExit(ed, *window)) {
                break;
            }
        }

        // 再生中のAnimationを評価し、対象Cubeのcframeを上書きする
        // (processInput内のapplyBodyAnimationより後に行うことでアニメーションを優先させる)
        if (isPlaying && !isPaused && user->humanoid) {
            user->humanoid->updateAnimation(deltaTime);
        }

        // Humanoidのパーツ配置(processInput内のapplyBodyAnimation)が終わった直後に、
        // アンカー駆動のキネマティックWeld(帽子等)を即時同期して追従ラグを無くす
        if (isPlaying && !isPaused && workspace->getPhysicsEngine()) {
            workspace->getPhysicsEngine()->syncWeldKinematics();
        }

        // ---- Pキー: Workspace 切り替え ----
        if (user->consumeWorkspaceSwitchRequest() && isPlaying) {
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

        beta({
            Vector3 centerPos = user->cpos;
            if (user->humanoid && user->humanoid->Root) {
                centerPos = user->humanoid->Root->getWorldCFrame().Position;
            }
            for (auto& [name, child] : workspace->getChildren()) {
                if (child->IsA("Terrain")) {
                    static_cast<Terrain*>(child.get())->update(centerPos);
                    break;
                }
            }
        });
        // ---- 描画 ----
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace.get());

        audioService->updateSounds(user->cpos, user->right);
    }

    std::cout << "[DEBUG] Main loop ended.\n";
    RCBN_LOG("Shutting down...");

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
    // Terrainは各Workspaceの子インスタンスとして受け渡されるため、systemデストラクタで自動解放される
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
