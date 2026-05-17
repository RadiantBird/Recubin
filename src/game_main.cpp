#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/CharacterSetting.hpp>
#include <Instances/Decal.hpp>

#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/AudioService.hpp>
#include <Editor/NullEditorManager.hpp>

#include <Util/Logger.hpp>
#include <yaml-cpp/yaml.h>
#include "include/stb_image.h"

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

// ===================================================
//  ヘルパー
// ===================================================
static CharacterSetting* findCharacterSetting(Instance* inst) {
    if (!inst) return nullptr;
    if (inst->GetClassName() == "CharacterSetting") return static_cast<CharacterSetting*>(inst);
    for (auto& [name, child] : inst->children) {
        if (auto* found = findCharacterSetting(child.get())) return found;
    }
    return nullptr;
}

static void applyAppIcon(GLFWwindow* window, Instance* root) {
    if (!window || !root) return;
    for (auto& [name, child] : root->children) {
        if (child->GetClassName() == "AppImage") {
            auto* ai = static_cast<AppImage*>(child.get());
            if (ai->iconPath.empty()) return;
            int w, h, ch;
            // OpenGL のテクスチャ座標系は左下が原点だが、Windows のアイコンは左上が原点なので、読み込み時に上下反転させる必要がある
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load(ai->iconPath.c_str(), &w, &h, &ch, 4);
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
    GameConfig cfg = loadStartup();

    // ---- ウィンドウ作成 ----
    if (!glfwInit()) return -1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, cfg.gameName.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    if (glewInit() != GLEW_OK) return -1;

    // ---- コアシステム初期化 ----
    auto renderer     = std::make_unique<Renderer>();
    auto physics      = std::make_unique<Physics>();
    auto audioService = std::make_unique<AudioService>();
    auto system       = std::make_shared<System>();
    auto luauEngine   = std::make_unique<LuauEngine>();
    auto user         = std::make_unique<User>(window);
    user->controlMode = User::ControlMode::Character;

    physics->init();
    renderer->init(window);
    renderer->editor = std::make_unique<NullEditorManager>();

    if (!audioService->initialize()) {
        RCBN_LOG("[ERROR] Failed to initialize AudioService.");
        return -1;
    }

    // ---- シングルトン構築 ----
    auto workspace = std::make_shared<Workspace>();
    auto lighting  = std::make_shared<Lighting>();
    lighting->Name = "Lighting";
    system->addChild(workspace);
    system->addChild(lighting);
    workspace->setPhysicsEngine(physics.get());

    SceneLoader::registerSingleton("System",    system);
    SceneLoader::registerSingleton("Workspace", workspace);
    SceneLoader::registerSingleton("Lighting",  lighting);
    SceneLoader::loadScene(cfg.startScene);
    SceneLoader::clearSingletons();

    applyAppIcon(window, system.get());

    luauEngine->setGlobalInstance(workspace->Name, workspace);
    luauEngine->setGlobalInstance("workspace", workspace);
    luauEngine->setGlobalInstance("System", system);
    luauEngine->setWorkspace(workspace);
    luauEngine->setSystem(system.get());
    physics->onContactCallback = [&](BaseCube* a, BaseCube* b) {
        luauEngine->onCollision(a, b);
    };

    // ---- ゲーム開始 ----
    CharacterSetting* cs = findCharacterSetting(system.get());
    user->spawnCharacter(cs);
    if (cs && !cs->facePath.empty()) {
        unsigned int faceTexID = renderer->loadTexture(cs->facePath.c_str());
        if (faceTexID && user->head)
            user->head->addChild(std::make_shared<Decal>(faceTexID, Face::Front));
    }
    audioService->playAutoPlaySounds();
    if (user->character) workspace->addChild(user->character);

    float lastFrame = static_cast<float>(glfwGetTime());

    // ---- メインループ（常にプレイ状態） ----
    while (!glfwWindowShouldClose(window)) {
        float now       = static_cast<float>(glfwGetTime());
        float deltaTime = now - lastFrame;
        lastFrame       = now;

        physics->update(*workspace, deltaTime);
        luauEngine->fireHeartbeat(deltaTime);
        luauEngine->update(deltaTime);
        luauEngine->executeWorkspaceScripts();

        user->processInput(*physics);
        if (user->wannaExit) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer->render(*user, window, *workspace);

        audioService->updateSounds(user->cpos, user->right);
    }

    // ---- クリーンアップ ----
    physics->clearCubes();
    workspace->setPhysicsEngine(nullptr);
    system->removeChild(workspace->Name);
    workspace.reset();
    system.reset();

    glfwTerminate();
    return 0;
}
