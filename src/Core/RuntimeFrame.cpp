#include <Core/RuntimeFrame.hpp>

#include <Core/LuauEngine.hpp>
#include <Core/Physics.hpp>
#include <Core/User.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/PathfindingService.hpp>
#include <Instances/System.hpp>
#include <Instances/Weather.hpp>
#include <Core/SceneRuntime.hpp>
#include <Util/FrameProfiler.hpp>

namespace RuntimeFrame {

bool simulateScripts(LuauEngine& luau, Workspace& workspace,
                     float deltaSeconds, Physics* physics) {
    FrameProfiler::get().beginSection("physics");
    if (physics) physics->update(workspace, deltaSeconds);
    FrameProfiler::get().endSection("physics");
    FrameProfiler::get().beginSection("luau");
    const auto stopForPathfinding = [&luau]() {
        const bool safe = !luau.consumeSafetyHaltRequest();
        FrameProfiler::get().endSection("luau");
        return safe;
    };
    luau.resetFrameSafetyCounters();
    luau.fireHeartbeat(deltaSeconds);
    if (PathfindingService::IsBuildActive()) return stopForPathfinding();
    luau.update(deltaSeconds);
    if (PathfindingService::IsBuildActive()) return stopForPathfinding();
    luau.executeWorkspaceScripts(workspace);
    if (PathfindingService::IsBuildActive()) return stopForPathfinding();
    luau.executeSystemScripts();
    if (PathfindingService::IsBuildActive()) return stopForPathfinding();
    const bool safe = !luau.consumeSafetyHaltRequest();
    FrameProfiler::get().endSection("luau");
    if (!safe) return false;

    return true;
}

bool processInput(User& user, Workspace& workspace, System& system,
                  float deltaSeconds, bool viewportFocused,
                  bool viewportHovered, bool gameplayInput,
                  bool wantsTextInput) {
    if (PathfindingService::IsBuildActive()) return !user.consumeExitRequest();
    user.allowControlModeSwitch = system.AllowControlModeSwitch;
    user.processInput(workspace.getPhysicsEngine(), deltaSeconds,
                      viewportFocused, viewportHovered, gameplayInput,
                      wantsTextInput);
    return !user.consumeExitRequest();
}

void simulateAfterInput(User& user, Workspace& workspace, float deltaSeconds,
                        Physics* physics) {
    if (PathfindingService::IsBuildActive()) return;
    Humanoid::updateAll(workspace.getRenderHumanoids(), deltaSeconds, physics);
    Vector3 terrainCenter = user.cpos;
    if (user.humanoid) {
        if (auto root = user.humanoid->getRootPart())
            terrainCenter = root->getWorldCFrame().Position;
    }
    SceneRuntime::updateTerrains(&workspace, terrainCenter);
    Weather::updateAll(&workspace, workspace.getRenderWeathers(), deltaSeconds,
                       user.cpos);
    ParticleEmitter::updateAll(workspace.getRenderParticleEmitters(), deltaSeconds);
}

} // namespace RuntimeFrame
