#include <recubin.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Core/AudioService.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/NullInputBackend.hpp>
#include <Core/GLFWInputBackend.hpp>
#include <Core/Physics.hpp>
#include <Core/Renderer.hpp>
#include <Core/SceneRuntime.hpp>
#include <Core/RuntimeFrame.hpp>
#include <Instances/PathfindingService.hpp>
#include <Core/SystemState.hpp>
#include <Editor/NullEditorManager.hpp>
#include <Instances/System.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace recubin {

namespace {
std::atomic<Engine*> g_activeEngine = nullptr;
}

struct Engine::Impl {
    EngineConfig config;
    GLFWwindow* window = nullptr;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<AudioService> audio;
    std::shared_ptr<System> system;
    std::shared_ptr<User> user;
    std::unique_ptr<LuauEngine> luau;
    std::unique_ptr<NullEditorManager> runtimeEditor;
    std::shared_ptr<Workspace> workspace;
    std::string error;
    bool initialized = false;
    std::atomic_bool stopRequested = false;
    bool glfwOwned = false;
};

Engine::Engine() : m_impl(std::make_unique<Impl>()) {}

Engine::~Engine() { shutdown(); }

bool Engine::initialize(const EngineConfig& config) {
    if (!m_impl) m_impl = std::make_unique<Impl>();
    shutdown();
    Engine* expected = nullptr;
    if (!g_activeEngine.compare_exchange_strong(expected, this)) {
        m_impl->error = "Only one Recubin engine instance may be active";
        return false;
    }
    m_impl->config = config;
    m_impl->error.clear();
    m_impl->stopRequested = false;

    if (!glfwInit()) {
        m_impl->error = "GLFW initialization failed";
        g_activeEngine.store(nullptr);
        return false;
    }
    m_impl->glfwOwned = true;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
    m_impl->window = glfwCreateWindow(config.windowWidth, config.windowHeight,
                                      config.windowTitle.c_str(), nullptr, nullptr);
    if (!m_impl->window) {
        m_impl->error = "OpenGL window creation failed";
        shutdown();
        return false;
    }
    glfwMakeContextCurrent(m_impl->window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        m_impl->error = "GLEW initialization failed";
        shutdown();
        return false;
    }

    m_impl->renderer = std::make_unique<Renderer>();
    m_impl->audio = std::make_unique<AudioService>();
    m_impl->system = std::make_shared<System>();
    m_impl->user = std::make_shared<User>(std::make_unique<GLFWInputBackend>(m_impl->window));
    m_impl->luau = std::make_unique<LuauEngine>();
    m_impl->renderer->init(m_impl->window);
    m_impl->runtimeEditor = std::make_unique<NullEditorManager>();
    m_impl->renderer->editor = std::move(m_impl->runtimeEditor);
    if (!m_impl->audio->initialize()) {
        m_impl->error = "AudioService initialization failed";
        shutdown();
        return false;
    }

    m_impl->initialized = true;
    if (!loadScene(config.scenePath)) {
        shutdown();
        return false;
    }
    SystemState::get().isPlaying = true;
    SystemState::get().viewportFocused = true;
    SystemState::get().inputState = InputState::Gameplay;
    return true;
}

bool Engine::loadScene(const std::string& scenePath) {
    if (!m_impl) return false;
    if (!m_impl->initialized || !m_impl->system || !m_impl->user || !m_impl->luau) {
        m_impl->error = "Engine must be initialized before loading a scene";
        return false;
    }
    auto staged = SceneRuntime::stageSceneLoad(scenePath, m_impl->system, m_impl->user);
    if (!staged) {
        m_impl->error = staged.message.empty() ? "Scene loading failed" : staged.message;
        return false;
    }
    const auto oldWorkspaces = SceneRuntime::collectWorkspaces(m_impl->system);
    m_impl->luau->cancelAllTasks();
    SceneRuntime::releaseTerrainStreamers(oldWorkspaces);
    for (const auto& oldWorkspace : oldWorkspaces) {
        if (oldWorkspace && oldWorkspace->getPhysicsEngine()) {
            oldWorkspace->getPhysicsEngine()->clearCubes();
            oldWorkspace->setPhysicsEngine(nullptr);
        }
    }
    auto bound = SceneRuntime::commitAndBind(std::move(staged), m_impl->system,
                                             m_impl->user, *m_impl->luau,
                                             m_impl->window);
    if (bound.loadStatus != SceneLoader::LoadStatus::Success || !bound.workspace) {
        m_impl->error = bound.loadMessage.empty() ? "Scene loading failed" : bound.loadMessage;
        return false;
    }
    m_impl->workspace = bound.workspace;
    m_impl->error.clear();
    for (const auto& workspace : bound.workspaces) {
        if (workspace && !workspace->getPhysicsEngine()) workspace->initPhysics();
    }
    SceneRuntime::applyDefaultCameraMode(*m_impl->system, *m_impl->user);
    return true;
}

bool Engine::step(float deltaSeconds) {
    if (!m_impl) return false;
    if (!m_impl->initialized || !m_impl->workspace || !m_impl->window) {
        m_impl->error = "Engine is not initialized";
        return false;
    }
    if (deltaSeconds < 0.0f || !std::isfinite(deltaSeconds)) {
        m_impl->error = "Invalid frame delta";
        return false;
    }
    glfwPollEvents();
    if (glfwWindowShouldClose(m_impl->window)) m_impl->stopRequested = true;
    SystemState::get().deltaTime = deltaSeconds;
    m_impl->luau->pollPathfindingRequests();
    bool navMeshBusy = PathfindingService::IsBuildActive();
    const bool simulationOk = navMeshBusy || RuntimeFrame::simulateScripts(
        *m_impl->luau, *m_impl->workspace, deltaSeconds,
        m_impl->workspace->getPhysicsEngine());
    if (!simulationOk) {
        m_impl->error = "Luau safety halt requested";
        m_impl->stopRequested = true;
    }
    if (navMeshBusy && m_impl->luau->consumeSafetyHaltRequest()) {
        m_impl->error = "Luau safety halt requested";
        m_impl->stopRequested = true;
    }
    navMeshBusy = PathfindingService::IsBuildActive();
    const bool uiInput = m_impl->renderer->isChatCapturingKeyboard() ||
                         ImGui::GetIO().WantTextInput;
    const bool inputOk = RuntimeFrame::processInput(
        *m_impl->user, *m_impl->workspace, *m_impl->system, deltaSeconds,
        !uiInput, !uiInput, !uiInput, uiInput);
    if (!inputOk) m_impl->stopRequested = true;
    navMeshBusy = navMeshBusy || PathfindingService::IsBuildActive();
    if (!navMeshBusy) {
        RuntimeFrame::simulateAfterInput(*m_impl->user, *m_impl->workspace,
                                         deltaSeconds,
                                         m_impl->workspace->getPhysicsEngine());
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_impl->renderer->render(*m_impl->user, m_impl->window, *m_impl->workspace);
    m_impl->audio->updateSounds(m_impl->user->cpos, m_impl->user->right);
    return !m_impl->stopRequested;
}

int Engine::run() {
    if (!m_impl) return -1;
    if (!m_impl->initialized) {
        m_impl->error = "Engine is not initialized";
        return -1;
    }
    double previousTime = glfwGetTime();
    while (!m_impl->stopRequested && !glfwWindowShouldClose(m_impl->window)) {
        const double currentTime = glfwGetTime();
        const float deltaSeconds = static_cast<float>(std::clamp(
            currentTime - previousTime, 0.0, 0.25));
        previousTime = currentTime;
        if (!step(deltaSeconds)) break;
    }
    return m_impl->error.empty() ? 0 : -1;
}

void Engine::requestStop() {
    if (m_impl) m_impl->stopRequested = true;
}

void Engine::shutdown() {
    if (!m_impl) return;
    if (m_impl->luau) {
        m_impl->luau->cancelAllTasks();
        m_impl->luau->resetSystemScripts();
    }
    if (m_impl->audio) m_impl->audio->uninit();
    const auto workspaces = m_impl->system
        ? SceneRuntime::collectWorkspaces(m_impl->system)
        : std::vector<std::shared_ptr<Workspace>>{};
    SceneRuntime::releaseTerrainStreamers(workspaces);
    for (const auto& workspace : workspaces) {
        if (workspace && workspace->getPhysicsEngine()) {
            workspace->getPhysicsEngine()->clearCubes();
            workspace->setPhysicsEngine(nullptr);
        }
    }
    m_impl->workspace.reset();
    m_impl->system.reset();
    m_impl->user.reset();
    m_impl->luau.reset();
    m_impl->renderer.reset();
    if (m_impl->window) {
        glfwDestroyWindow(m_impl->window);
        m_impl->window = nullptr;
    }
    if (m_impl->glfwOwned) {
        glfwTerminate();
        m_impl->glfwOwned = false;
    }
    m_impl->audio.reset();
    m_impl->initialized = false;
    Engine* expected = this;
    g_activeEngine.compare_exchange_strong(expected, nullptr);
}

bool Engine::isInitialized() const noexcept { return m_impl && m_impl->initialized; }

const std::string& Engine::lastError() const noexcept {
    static const std::string empty;
    return m_impl ? m_impl->error : empty;
}

std::shared_ptr<Workspace> Engine::workspace() const noexcept {
    return m_impl ? m_impl->workspace : nullptr;
}

} // namespace recubin
