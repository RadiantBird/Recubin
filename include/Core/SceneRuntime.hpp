#pragma once
#include <string>
#include <vector>
#include <memory>

class System;
class User;
class Workspace;
class Instance;
class LuauEngine;
struct GLFWwindow;

namespace SceneRuntime {

    struct Bound {
        std::shared_ptr<Workspace> workspace;
        std::vector<std::shared_ptr<Workspace>> workspaces;
    };

    // singleton登録 → loadScene → clearSingletons → User確保 →
    // collectWorkspaces（空ならデフォルトWS+Lighting生成） → applyAppIcon →
    // bindStandardGlobals を一括実行して Bound を返す。
    // 物理初期化・Lighting移行・editor固有処理は呼び出し側に残すこと。
    Bound loadAndBind(const std::string& scenePath,
                      const std::shared_ptr<System>& system,
                      const std::shared_ptr<User>& user,
                      LuauEngine& engine,
                      GLFWwindow* window);

    std::vector<std::shared_ptr<Workspace>> collectWorkspaces(const std::shared_ptr<System>& system);

    void applyAppIcon(GLFWwindow* window, Instance* root);

    // workspace->Name / "workspace" / "System" / "system" / "User" を一括登録
    void bindStandardGlobals(LuauEngine& engine,
                             const std::shared_ptr<Workspace>& workspace,
                             const std::shared_ptr<System>& system,
                             const std::shared_ptr<User>& user);

} // namespace SceneRuntime
