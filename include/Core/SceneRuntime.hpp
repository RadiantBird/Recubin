#pragma once
#include <string>
#include <vector>
#include <memory>
#include <Core/SceneLoader.hpp>

class System;
class User;
class Workspace;
class Instance;
class LuauEngine;
class Terrain;
struct Vector3;
struct GLFWwindow;

namespace SceneRuntime {

    struct StagedSceneLoad {
        std::shared_ptr<System> system;
        std::shared_ptr<User> user;
        SceneLoader::SceneDocumentMetadata metadata;
        std::string scenePath;
        SceneLoader::LoadStatus status = SceneLoader::LoadStatus::Success;
        std::string message;

        explicit operator bool() const {
            return status == SceneLoader::LoadStatus::Success && system && user;
        }
    };

    struct Bound {
        std::shared_ptr<Workspace> workspace;
        std::vector<std::shared_ptr<Workspace>> workspaces;
        SceneLoader::SceneDocumentMetadata metadata;
        std::string scenePath;
        SceneLoader::LoadStatus loadStatus = SceneLoader::LoadStatus::Success;
        std::string loadMessage;
    };

    // 隔離System/Userへ一度だけロードする。成功するまでliveツリーを変更しない。
    StagedSceneLoad stageSceneLoad(const std::string& scenePath,
                                   const std::shared_ptr<System>& liveSystem,
                                   const std::shared_ptr<User>& liveUser);

    // 成功済みStageをlive System/Userへ移植し、既定サービスとLuau globalsを構築する。
    Bound commitAndBind(StagedSceneLoad&& staged,
                        const std::shared_ptr<System>& system,
                        const std::shared_ptr<User>& user,
                        LuauEngine& engine,
                        GLFWwindow* window);

    // stageSceneLoad → commitAndBind の起動用便利関数。
    // 物理初期化・Lighting移行・editor固有処理は呼び出し側に残すこと。
    Bound loadAndBind(const std::string& scenePath,
                      const std::shared_ptr<System>& system,
                      const std::shared_ptr<User>& user,
                      LuauEngine& engine,
                      GLFWwindow* window);

    void applyDefaultCameraMode(const System& system, User& user);

    std::vector<std::shared_ptr<Workspace>> collectWorkspaces(const std::shared_ptr<System>& system);

    // Workspace配下のFolder/Modelを含めて全Terrainを列挙する。
    std::vector<Terrain*> collectTerrains(Instance* root);
    void updateTerrains(Workspace* workspace, const Vector3& centerPos);
    void releaseTerrainStreamers(const std::vector<std::shared_ptr<Workspace>>& workspaces);

    void applyAppIcon(GLFWwindow* window, Instance* root);

    // workspace->Name / "workspace" / "System" / "system" / "User" を一括登録
    void bindStandardGlobals(LuauEngine& engine,
                             const std::shared_ptr<Workspace>& workspace,
                             const std::shared_ptr<System>& system,
                             const std::shared_ptr<User>& user);

} // namespace SceneRuntime
