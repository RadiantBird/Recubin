#include <Core/SceneRuntime.hpp>
#include <Core/SceneLoader.hpp>
#include <Core/LuauEngine.hpp>
#include <Core/User.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/PathfindingService.hpp>
#include <include/GLFW/glfw3.h>
#include "include/stb_image.h"
#include <Util/AssetGuard.hpp>

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

void applyAppIcon(GLFWwindow* window, Instance* root) {
    if (!window || !root) return;
    for (auto& [name, child] : root->children) {
        if (child->getClassName() == "AppImage") {
            auto* ai = static_cast<AppImage*>(child.get());
            if (ai->iconPath.empty()) return;
            if (!AssetGuard::allow(ai->iconPath)) return;
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
}

Bound loadAndBind(const std::string& scenePath,
                  const std::shared_ptr<System>& system,
                  const std::shared_ptr<User>& user,
                  LuauEngine& engine,
                  GLFWwindow* window) {
    SceneLoader::registerSingleton("System", system);
    SceneLoader::registerSingleton("User", user);
    SceneLoader::loadScene(scenePath);
    SceneLoader::clearSingletons();

    // User が YAML に存在しなくてもシステム直下に確保する
    if (system->children.find("User") == system->children.end()) {
        system->addChild(user);
    }

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

    applyAppIcon(window, system.get());
    bindStandardGlobals(engine, workspace, system, user);

    return Bound{ workspace, workspaces };
}

} // namespace SceneRuntime
