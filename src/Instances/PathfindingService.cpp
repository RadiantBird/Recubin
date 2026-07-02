#include <include/Instances/PathfindingService.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <filesystem>

namespace {
// シーンファイルと同じディレクトリの .navcache/ 以下にWorkspaceごとのキャッシュファイルを置く
// 例: assets/scenes/pathfinder.yaml -> assets/scenes/.navcache/pathfinder_Workspace.navcache
std::string cacheFilePathFor(const std::string& scenePath, const std::string& workspaceName) {
    if (scenePath.empty()) return "";
    std::filesystem::path p(scenePath);
    std::string stem = p.stem().string();
    return (p.parent_path() / ".navcache" / (stem + "_" + workspaceName + ".navcache")).string();
}
} // namespace

// プロパティ・スキーマ（単一の正）。Luau/YAML/clone/エディターを一括駆動。
static const bool s_pathfindingServiceRegistered = []{
    using namespace PropertyRegistry;
    registerClass("PathfindingService", {
        field<&PathfindingService::AgentRadius>    ("AgentRadius",     0.1f, 10.0f, 0.1f).clampLua(),
        field<&PathfindingService::AgentHeight>    ("AgentHeight",     0.5f, 20.0f, 0.1f).clampLua(),
        field<&PathfindingService::AgentMaxClimb>  ("AgentMaxClimb",   0.0f, 10.0f, 0.1f).clampLua(),
        field<&PathfindingService::AgentMaxSlope>  ("AgentMaxSlope",   0.0f, 89.0f, 1.0f).clampLua(),
        field<&PathfindingService::MaxJumpDistance>("MaxJumpDistance", 0.0f, 50.0f, 0.1f).clampLua(),
        field<&PathfindingService::MaxJumpHeight>  ("MaxJumpHeight",   0.0f, 50.0f, 0.1f).clampLua(),
    });
    return true;
}();

PathfindingService* PathfindingService::s_active = nullptr;

PathfindingService::PathfindingService() : Instance("PathfindingService") {
    s_active = this;
}

PathfindingService::~PathfindingService() {
    if (s_active == this) s_active = nullptr;
}

bool PathfindingService::IsA(std::string className) {
    if (className == "PathfindingService") return true;
    return Instance::IsA(className);
}

void PathfindingService::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "PathfindingService", name, value)) return;
    Instance::setProperty(name, value);
}

std::vector<Pathfinding::PathWaypoint> PathfindingService::FindPath(Workspace* workspace, const Vector3& start, const Vector3& goal) {
    if (!workspace) return {};

    auto it = m_cache.find(workspace->Name);
    if (it == m_cache.end()) {
        Pathfinding::BuildSettings settings;
        settings.agentRadius     = AgentRadius;
        settings.agentHeight     = AgentHeight;
        settings.agentMaxClimb   = AgentMaxClimb;
        settings.agentMaxSlope   = AgentMaxSlope;
        settings.maxJumpDistance = MaxJumpDistance;
        settings.maxJumpHeight   = MaxJumpHeight;
        std::string cachePath = cacheFilePathFor(ScenePath, workspace->Name);
        it = m_cache.emplace(workspace->Name, Pathfinding::NavMesh::Build(workspace, settings, cachePath)).first;
    }

    if (!it->second) return {};
    return it->second->FindPath(start, goal);
}

void PathfindingService::Invalidate(Workspace* workspace) {
    if (!workspace) return;
    m_cache.erase(workspace->Name);
}

void PathfindingService::InvalidateActive(Workspace* workspace) {
    if (s_active) s_active->Invalidate(workspace);
}
