#include <Core/SceneRuntime.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/User.hpp>
#include <Core/NullInputBackend.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/PathfindingService.hpp>
#include <Instances/Folder.hpp>
#include <Instances/Users.hpp>
#include <Instances/ChatService.hpp>
#include <Core/Terrain.hpp>
#include <include/GLFW/glfw3.h>
#include "include/stb_image.h"
#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>

namespace SceneRuntime {

namespace {

void copySystemScalars(const System& source, System& destination) {
    destination.MaxClonesPerFrame = source.MaxClonesPerFrame;
    destination.MaxRestartsPerFrame = source.MaxRestartsPerFrame;
    destination.MaxTasksPerFrame = source.MaxTasksPerFrame;
    destination.ScriptLoopTimeoutSeconds = source.ScriptLoopTimeoutSeconds;
    destination.BaseResolution = source.BaseResolution;
    destination.UseNetwork = source.UseNetwork;
    destination.ApplicationId = source.ApplicationId;
    destination.EnableIOAPI = source.EnableIOAPI;
    destination.EnableIPCAPI = source.EnableIPCAPI;
    destination.EnableExternalFileAccess = source.EnableExternalFileAccess;
}

void copyUserScalars(const User& source, User& destination) {
    destination.controlMode = source.controlMode;
    destination.speed = source.speed;
    destination.rotationSpeed = source.rotationSpeed;
    destination.mouseRotationSpeed = source.mouseRotationSpeed;
    destination.characterSmoothing = source.characterSmoothing;
    YAML::Node inputEnabled;
    inputEnabled = source.isMovementInputEnabled();
    destination.setProperty("MovementInputEnabled", inputEnabled);
    inputEnabled = source.isCameraInputEnabled();
    destination.setProperty("CameraInputEnabled", inputEnabled);
    inputEnabled = source.isHotkeyInputEnabled();
    destination.setProperty("HotkeyInputEnabled", inputEnabled);
    inputEnabled = source.isToolInputEnabled();
    destination.setProperty("ToolInputEnabled", inputEnabled);
    destination.cameraDistance = source.cameraDistance;
    destination.zoomSpeed = source.zoomSpeed;
    destination.mouseZoomSpeed = source.mouseZoomSpeed;
}

void disconnectSceneSignals(const std::shared_ptr<System>& system,
                            const std::shared_ptr<User>& user) {
    if (system && system->Heartbeat) system->Heartbeat->disconnectAll();
    if (!user) return;
    user->resetInputRuntimeState();
    if (user->CharacterAdded) user->CharacterAdded->disconnectAll();
    if (user->ExitRequested) user->ExitRequested->disconnectAll();
    if (user->Input) {
        if (user->Input->Pressed) user->Input->Pressed->disconnectAll();
        if (user->Input->Released) user->Input->Released->disconnectAll();
    }
}

void removeAllChildren(const std::shared_ptr<Instance>& parent) {
    if (!parent) return;
    std::vector<std::string> names;
    names.reserve(parent->children.size());
    for (const auto& [name, child] : parent->children) {
        (void)child;
        names.push_back(name);
    }
    for (const auto& name : names) parent->removeChild(name);
}

void moveAllChildren(const std::shared_ptr<Instance>& source,
                     const std::shared_ptr<Instance>& destination) {
    if (!source || !destination) return;
    std::vector<std::shared_ptr<Instance>> children;
    children.reserve(source->children.size());
    for (const auto& [name, child] : source->children) {
        (void)name;
        children.push_back(child);
    }
    for (const auto& child : children) destination->addChild(child);
}

std::shared_ptr<Users> findUsers(const std::shared_ptr<System>& system) {
    if (!system) return nullptr;
    for (const auto& [name, child] : system->children) {
        (void)name;
        if (child && child->IsA("Users"))
            return std::static_pointer_cast<Users>(child);
    }
    return nullptr;
}

} // namespace

std::vector<std::shared_ptr<Workspace>> collectWorkspaces(const std::shared_ptr<System>& system) {
    std::vector<std::shared_ptr<Workspace>> result;
    if (!system) return result;
    for (auto& [name, child] : system->children) {
        if (child && child->IsA("Workspace"))
            result.push_back(std::static_pointer_cast<Workspace>(child));
    }
    return result;
}

std::vector<Terrain*> collectTerrains(Instance* root) {
    std::vector<Terrain*> result;
    if (!root) return result;
    auto visit = [&](auto& self, Instance* value) -> void {
        if (!value) return;
        if (value->IsA("Terrain")) result.push_back(static_cast<Terrain*>(value));
        for (const auto& [name, child] : value->getChildren()) {
            (void)name;
            self(self, child.get());
        }
    };
    visit(visit, root);
    return result;
}

void updateTerrains(Workspace* workspace, const Vector3& centerPos) {
    for (Terrain* terrain : collectTerrains(workspace)) {
        if (terrain) terrain->update(centerPos);
    }
}

void releaseTerrainStreamers(
    const std::vector<std::shared_ptr<Workspace>>& workspaces) {
    for (const auto& workspace : workspaces) {
        for (Terrain* terrain : collectTerrains(workspace.get())) {
            if (terrain) terrain->releaseStreamer();
        }
    }
}

void applyAppIcon(GLFWwindow* window, Instance* root) {
    if (!root) return;
    std::string iconPath;
    for (auto& [name, child] : root->children) {
        if (child->getClassName() == "AppImage") {
            auto* ai = static_cast<AppImage*>(child.get());
            iconPath = ai->iconPath;
            break;
        }
    }

    if (iconPath.empty()) {
        // macOSではシーン切り替え前のDockアイコンが残らないよう、既定値へ戻す。
        // Windows/MockはUnsupportedを返すため、従来どおり何もしない。
        (void)getPlatform().setApplicationIcon({});
        return;
    }
    if (!AssetGuard::allow(iconPath)) return;

    const std::string normalizedPath = AssetPath::normalize(iconPath);
    const ApplicationIconResult platformResult =
        getPlatform().setApplicationIcon(normalizedPath);
    if (platformResult == ApplicationIconResult::Applied) return;
    if (platformResult == ApplicationIconResult::Failed) {
        RCBN_WARN("Failed to load application icon: " << normalizedPath);
        return;
    }

    // Windowsでは従来どおり、GLFWのウィンドウアイコンとして設定する。
    if (!window) return;
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* px = stbi_load(normalizedPath.c_str(), &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(1);
    if (px) {
        GLFWimage img{ w, h, px };
        glfwSetWindowIcon(window, 1, &img);
        stbi_image_free(px);
    }
}

void bindStandardGlobals(LuauEngine& engine,
                         const std::shared_ptr<Workspace>& workspace,
                         const std::shared_ptr<System>& system,
                         const std::shared_ptr<User>& user)
{
    engine.setGlobalInstance(workspace->Name, workspace);
    engine.setGlobalInstance("workspace", workspace);
    engine.setGlobalInstance("System", system);
    engine.setGlobalInstance("system", system);
    engine.setGlobalInstance("User", user);
    engine.setWorkspace(workspace);
    engine.setSystem(system.get());
    // シーンをまたいで旧インスタンスキーのモジュール返り値が残らないように破棄する
    engine.clearModuleCache();
    // 旧シーン向けのtask.delayコールバックが新シーンで発火しないように破棄する
    engine.cancelAllTasks();
}

Bound loadAndBind(const std::string& scenePath,
                  const std::shared_ptr<System>& system,
                  const std::shared_ptr<User>& user,
                  LuauEngine& engine,
                  GLFWwindow* window) {
    auto staged = stageSceneLoad(scenePath, system, user);
    if (!staged) {
        Bound failed;
        failed.metadata = staged.metadata;
        failed.scenePath = scenePath;
        failed.loadStatus = staged.status;
        failed.loadMessage = staged.message;
        return failed;
    }
    return commitAndBind(std::move(staged), system, user, engine, window);
}

StagedSceneLoad stageSceneLoad(const std::string& scenePath,
                               const std::shared_ptr<System>& liveSystem,
                               const std::shared_ptr<User>& liveUser) {
    StagedSceneLoad staged;
    staged.scenePath = scenePath;
    staged.system = std::make_shared<System>();
    staged.user = std::make_shared<User>(std::make_unique<NullInputBackend>(), true);
    if (liveSystem) copySystemScalars(*liveSystem, *staged.system);
    if (liveUser) copyUserScalars(*liveUser, *staged.user);

    if (scenePath.empty()) return staged;

    SceneLoader::LoadContext context;
    context.registerMergeInstance("System", staged.system);
    context.registerMergeInstance("User", staged.user);
    const auto result = SceneLoader::loadSceneResult(scenePath, context);
    staged.metadata.version = result.metadata.version;
    staged.metadata.characterAnimationBindingsVersion = result.metadata.characterAnimationBindingsVersion;
    staged.metadata.legacyDefaultR6AnimationDecision = result.metadata.legacyDefaultR6AnimationDecision;
    staged.metadata.legacyWalkContentPath = result.metadata.legacyWalkContentPath;
    staged.metadata.applicationIdGenerated = result.metadata.applicationIdGenerated;
    staged.status = result.status;
    staged.message = result.message;
    if (result && result.root != staged.system && result.root != staged.user &&
        result.root->Parent.expired()) {
        staged.system->addChild(result.root);
    }
    return staged;
}

Bound commitAndBind(StagedSceneLoad&& staged,
                    const std::shared_ptr<System>& system,
                    const std::shared_ptr<User>& user,
                    LuauEngine& engine,
                    GLFWwindow* window) {
    if (!staged || !system || !user) {
        Bound failed;
        failed.metadata.version = staged.metadata.version;
        failed.metadata.characterAnimationBindingsVersion = staged.metadata.characterAnimationBindingsVersion;
        failed.metadata.legacyDefaultR6AnimationDecision = staged.metadata.legacyDefaultR6AnimationDecision;
        failed.metadata.legacyWalkContentPath = staged.metadata.legacyWalkContentPath;
        failed.metadata.applicationIdGenerated = staged.metadata.applicationIdGenerated;
        failed.scenePath = staged.scenePath;
        failed.loadStatus = staged.status == SceneLoader::LoadStatus::Success
            ? SceneLoader::LoadStatus::YamlError : staged.status;
        failed.loadMessage = staged.message.empty() ? "Invalid staged scene" : staged.message;
        return failed;
    }

    copySystemScalars(*staged.system, *system);
    copyUserScalars(*staged.user, *user);

    auto stagedUsers = findUsers(staged.system);
    if (!stagedUsers) {
        stagedUsers = std::make_shared<Users>();
        staged.system->addChild(stagedUsers);
    }

    // User配下を隔離Userからlive Userへ移す。入力、Signal、camera、identityはlive側を維持する。
    removeAllChildren(std::static_pointer_cast<Instance>(user));
    user->resetInventory();
    moveAllChildren(std::static_pointer_cast<Instance>(staged.user),
                    std::static_pointer_cast<Instance>(user));
    if (auto stagedParent = staged.user->Parent.lock())
        stagedParent->removeChild(staged.user->Name);

    // 旧ツリーを破棄してから、staged Users内のUserをlive Userへ置換して全子を移植する。
    disconnectSceneSignals(system, user);
    removeAllChildren(std::static_pointer_cast<Instance>(system));
    stagedUsers->addChild(user);
    moveAllChildren(std::static_pointer_cast<Instance>(staged.system),
                    std::static_pointer_cast<Instance>(system));

    if (auto it = user->children.find("Inventory"); it != user->children.end()) {
        if (auto folder = std::dynamic_pointer_cast<Folder>(it->second))
            user->Inventory = folder;
    }
    if (!user->Inventory || user->Inventory->Parent.lock().get() != user.get()) {
        user->resetInventory();
        user->initializeInventory();
    }
    user->syncToolsFromInventory();

    auto workspaces = collectWorkspaces(system);
    if (workspaces.empty()) {
        auto ws = std::make_shared<Workspace>();
        auto li = std::make_shared<Lighting>();
        li->Name = "Lighting";
        system->addChild(ws);
        ws->addChild(li);
        workspaces = collectWorkspaces(system);
    }
    auto workspace = workspaces.front();

    // PathfindingService も Workspace 同様、System直下に無ければ自動生成する
    if (system->children.find("PathfindingService") == system->children.end()) {
        auto pathfinding = std::make_shared<PathfindingService>();
        system->addChild(pathfinding);
    }
    // NavMeshディスクキャッシュのパス算出に使うため、シーン読み込みのたびに更新する
    if (auto it = system->children.find("PathfindingService"); it != system->children.end()) {
        static_cast<PathfindingService*>(it->second.get())->ScenePath = staged.scenePath;
    }

    if (system->children.find("ChatService") == system->children.end()) {
        system->addChild(std::make_shared<ChatService>());
    }

    // staged Userをlive Userへ置換し、既定サービスも揃った最終ツリーで参照を張り直す。
    SceneLoader::resolveConstraintRefs(system.get());

    applyAppIcon(window, system.get());
    bindStandardGlobals(engine, workspace, system, user);
    if (auto it = system->children.find("ChatService"); it != system->children.end())
        engine.setGlobalInstance("ChatService", it->second);

    return Bound{ workspace, workspaces, staged.metadata, staged.scenePath,
                  SceneLoader::LoadStatus::Success, {} };
}

} // namespace SceneRuntime
