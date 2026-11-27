#include <recubin.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    recubin::EngineConfig config;
    config.windowTitle = "Recubin C++ embedding example";
    config.windowWidth = 960;
    config.windowHeight = 540;
    config.visible = argc <= 1 || std::string(argv[1]) != "--headless";
    config.scenePath = (argc > 1 && std::string(argv[1]) != "--headless")
        ? argv[1] : "sample_scene.yaml";

    recubin::Engine engine;
    if (!engine.initialize(config)) {
        std::cerr << "Recubin initialization failed: " << engine.lastError() << '\n';
        return 1;
    }
    auto workspace = engine.workspace();
    if (!workspace) {
        std::cerr << "Recubin did not provide a Workspace\n";
        return 1;
    }
    auto cube = std::make_shared<Cube>(Vector3(0.0f, 0.0f, 0.0f),
                                       Vector3(2.0f, 2.0f, 2.0f), TextureHandle{});
    cube->Name = "EmbeddedCube";
    workspace->addChild(cube);
    for (int frame = 0; frame < 3; ++frame) {
        if (!engine.step(1.0f / 60.0f)) {
            std::cerr << "Recubin frame failed: " << engine.lastError() << '\n';
            return 1;
        }
    }
    auto scriptValue = workspace->getChild("ScriptValue");
    if (!scriptValue || scriptValue->getClassName() != "StringValue" ||
        static_cast<StringValue*>(scriptValue)->Value != "luau-ran") {
        std::cerr << "Luau did not update ScriptValue\n";
        return 1;
    }

    recubin::Engine secondEngine;
    recubin::EngineConfig secondConfig;
    secondConfig.visible = false;
    if (secondEngine.initialize(secondConfig)) {
        std::cerr << "A second active Recubin engine was accepted\n";
        return 1;
    }
    const auto workspaceBeforeFailure = engine.workspace();
    if (engine.loadScene("missing-embedding-scene.yaml") ||
        engine.workspace() != workspaceBeforeFailure) {
        std::cerr << "A failed scene load did not preserve the active Workspace\n";
        return 1;
    }
    if (!engine.loadScene("sample_scene_alt.yaml") ||
        engine.workspace() == workspaceBeforeFailure) {
        std::cerr << "A valid scene load did not switch the Workspace\n";
        return 1;
    }
    engine.shutdown();
    recubin::EngineConfig reinitializeConfig;
    reinitializeConfig.scenePath = "sample_scene.yaml";
    reinitializeConfig.visible = false;
    if (!engine.initialize(reinitializeConfig) || !engine.step(1.0f / 60.0f)) {
        std::cerr << "Recubin reinitialization failed: " << engine.lastError() << '\n';
        return 1;
    }
    auto reinitializedValue = engine.workspace()->getChild("ScriptValue");
    if (!reinitializedValue || reinitializedValue->getClassName() != "StringValue" ||
        static_cast<StringValue*>(reinitializedValue)->Value != "luau-ran") {
        std::cerr << "Luau did not run after reinitialization\n";
        return 1;
    }

    int runResult = -1;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        engine.requestStop();
    });
    runResult = engine.run();
    stopper.join();
    if (runResult != 0) {
        std::cerr << "Recubin run failed: " << engine.lastError() << '\n';
        return 1;
    }
    engine.shutdown();
    std::cout << "Recubin embedding example passed\n";
    return 0;
}
