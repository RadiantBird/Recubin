#include <Core/SceneRuntime.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/User.hpp>
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
    SceneLoader::registerSingleton("System", system);
    SceneLoader::registerSingleton("User", user);

    // Users コンテナが reload をまたいで温存されている場合(resetSystemForReload参照)、
    // System/User と同様に事前登録してマージ対象にする。登録しないと loadScene() が
    // YAML中のUsersノードを新規生成してしまい、温存済みコンテナとキー衝突して増殖する。
    std::shared_ptr<Instance> usersContainer;
    if (auto it = system->children.find("Users"); it != system->children.end()) {
        usersContainer = it->second;
        SceneLoader::registerSingleton("Users", usersContainer);
    }

    const auto loadResult = SceneLoader::loadSceneResult(scenePath);
    const auto metadata = loadResult.metadata;
    SceneLoader::clearSingletons();

    // Users コンテナが無ければ自動生成する(Workspace/PathfindingServiceと同様のパターン)
    if (!usersContainer) {
        if (auto it = system->children.find("Users"); it != system->children.end()) {
            usersContainer = it->second;
        } else {
            usersContainer = std::make_shared<Users>();
            system->addChild(usersContainer);
        }
    }

    // User が YAML に存在しなくてもSystem/Users直下に確保する
    if (usersContainer->children.find("User") == usersContainer->children.end()) {
        usersContainer->addChild(user);
    }

    // Inventory: シーンYAMLにUser直下のFolder("Inventory")が保存されていれば、それを
    // user->Inventory として採用する（tree上の実体とuser->Inventoryメンバが乖離すると、
    // 装備中Toolの追跡等がずれるため）。保存されていなければ新しい空Inventoryを生成する。
    if (auto it = user->children.find("Inventory"); it != user->children.end()) {
        if (auto folder = std::dynamic_pointer_cast<Folder>(it->second)) {
            user->Inventory = folder;
        }
    } else {
        // メンバに残っている旧Inventoryは再利用しない。シーンにInventoryが無い場合も
        // 毎回新しい空Folderを作り、前回の子やTool参照を持ち越さない。
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
        static_cast<PathfindingService*>(it->second.get())->ScenePath = scenePath;
    }

    if (system->children.find("ChatService") == system->children.end()) {
        system->addChild(std::make_shared<ChatService>());
    }

    applyAppIcon(window, system.get());
    bindStandardGlobals(engine, workspace, system, user);
    if (auto it = system->children.find("ChatService"); it != system->children.end())
        engine.setGlobalInstance("ChatService", it->second);

    return Bound{ workspace, workspaces, metadata, scenePath, loadResult.status, loadResult.message };
}

} // namespace SceneRuntime
