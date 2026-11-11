#pragma once

#include <memory>
#include <string>

#include <Instances/Instance.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Model.hpp>
#include <Instances/StringValue.hpp>
#include <Math/Vector3.hpp>
#include <Math/CFrame.hpp>

namespace recubin {

struct EngineConfig {
    std::string scenePath;
    std::string windowTitle = "Recubin";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool visible = true;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool initialize(const EngineConfig& config = {});
    bool loadScene(const std::string& scenePath);
    bool step(float deltaSeconds);
    int run();
    void requestStop();
    void shutdown();

    bool isInitialized() const noexcept;
    const std::string& lastError() const noexcept;
    std::shared_ptr<Workspace> workspace() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace recubin
