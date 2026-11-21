#include <include/Instances/PathfindingService.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>

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

struct PathfindingService::BuildJob {
    std::string workspaceKey;
    uint64_t generation = 0;
    std::shared_ptr<Pathfinding::BuildControl> control;
    std::thread worker;
    std::unique_ptr<Pathfinding::NavMesh> result;
    std::atomic<bool> completed{false};
    double elapsedMilliseconds = 0.0;

    ~BuildJob() {
        if (control) control->cancel();
        if (worker.joinable()) worker.join();
    }
};

struct PathfindingService::RequestRecord {
    std::string workspaceKey;
    uint64_t generation = 0;
    Vector3 start;
    Vector3 goal;
    RequestStatus status = RequestStatus::Pending;
    std::vector<Pathfinding::PathWaypoint> waypoints;
};

PathfindingService::PathfindingService() : Instance("PathfindingService") {
    s_active = this;
}

PathfindingService::~PathfindingService() {
    CancelPending();
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

Pathfinding::BuildSettings PathfindingService::currentSettings() const {
    Pathfinding::BuildSettings settings;
    settings.agentRadius     = AgentRadius;
    settings.agentHeight     = AgentHeight;
    settings.agentMaxClimb   = AgentMaxClimb;
    settings.agentMaxSlope   = AgentMaxSlope;
    settings.maxJumpDistance = MaxJumpDistance;
    settings.maxJumpHeight   = MaxJumpHeight;
    return settings;
}

void PathfindingService::startBuild(
        Workspace* workspace, const std::string& workspaceKey, uint64_t generation) {
    if (!workspace || m_builds.find(workspaceKey) != m_builds.end()) return;

    // Instance/Terrainの読み取りはここ（呼び出し元のメインスレッド）で完結させる。
    Pathfinding::GeometrySnapshot snapshot =
        Pathfinding::NavMesh::CaptureGeometry(workspace);
    if (snapshot.empty()) {
        for (auto& [requestId, request] : m_requests) {
            if (request->status == RequestStatus::Pending &&
                request->workspaceKey == workspaceKey &&
                request->generation == generation) {
                request->status = RequestStatus::Failed;
            }
        }
        return;
    }

    auto job = std::make_unique<BuildJob>();
    job->workspaceKey = workspaceKey;
    job->generation = generation;
    job->control = std::make_shared<Pathfinding::BuildControl>();
    BuildJob* jobPtr = job.get();
    const Pathfinding::BuildSettings settings = currentSettings();
    const std::string cachePath = cacheFilePathFor(ScenePath, workspaceKey);
    job->worker = std::thread(
        [jobPtr, snapshot = std::move(snapshot), settings, cachePath]() mutable {
            const auto startedAt = std::chrono::steady_clock::now();
            jobPtr->result = Pathfinding::NavMesh::BuildSnapshot(
                std::move(snapshot), settings, cachePath, jobPtr->control.get());
            jobPtr->elapsedMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startedAt).count();
            jobPtr->completed.store(true, std::memory_order_release);
        });
    m_builds.emplace(workspaceKey, std::move(job));
}

void PathfindingService::finishCompletedBuilds() {
    for (auto buildIt = m_builds.begin(); buildIt != m_builds.end();) {
        BuildJob& job = *buildIt->second;
        if (!job.completed.load(std::memory_order_acquire)) {
            ++buildIt;
            continue;
        }
        if (job.worker.joinable()) job.worker.join();
        const uint32_t tileCount =
            job.control->totalTiles.load(std::memory_order_acquire);
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const unsigned int availableWorkers =
            hardwareThreads > 1 ? hardwareThreads - 1 : 1;
        const unsigned int workerCount =
            tileCount == 0 ? 0 : std::max(1U, std::min(tileCount, availableWorkers));
        RCBN_LOG("NavMesh build: workspace=" << job.workspaceKey
                 << " tiles=" << tileCount
                 << " workers=" << workerCount
                 << " elapsed=" << job.elapsedMilliseconds << "ms");

        const auto generationIt = m_generations.find(job.workspaceKey);
        const uint64_t currentGeneration =
            generationIt == m_generations.end() ? 0 : generationIt->second;
        const bool generationMatches = currentGeneration == job.generation;
        const bool succeeded = generationMatches && static_cast<bool>(job.result);
        Pathfinding::NavMesh* navMesh = nullptr;
        if (succeeded) {
            navMesh = job.result.get();
            m_cache[job.workspaceKey] = std::move(job.result);
        }

        for (auto& [requestId, request] : m_requests) {
            if (request->status != RequestStatus::Pending ||
                request->workspaceKey != job.workspaceKey ||
                request->generation != job.generation) {
                continue;
            }

            if (succeeded) {
                request->waypoints =
                    navMesh->FindPath(request->start, request->goal);
                request->status = RequestStatus::Ready;
            } else {
                request->status =
                    job.control->stage.load(std::memory_order_acquire) ==
                            Pathfinding::BuildStage::Cancelled
                        ? RequestStatus::Cancelled
                        : RequestStatus::Failed;
            }
        }

        buildIt = m_builds.erase(buildIt);
    }
}

void PathfindingService::cancelBuild(const std::string& workspaceKey) {
    const auto buildIt = m_builds.find(workspaceKey);
    if (buildIt == m_builds.end()) return;

    BuildJob& job = *buildIt->second;
    job.control->cancel();
    if (job.worker.joinable()) job.worker.join();
    for (auto& [requestId, request] : m_requests) {
        if (request->status == RequestStatus::Pending &&
            request->workspaceKey == workspaceKey &&
            request->generation == job.generation) {
            request->status = RequestStatus::Cancelled;
        }
    }
    m_builds.erase(buildIt);
}

std::vector<Pathfinding::PathWaypoint> PathfindingService::FindPath(Workspace* workspace, const Vector3& start, const Vector3& goal) {
    if (!workspace) return {};

    finishCompletedBuilds();
    cancelBuild(workspace->Name);
    auto it = m_cache.find(workspace->Name);
    if (it == m_cache.end()) {
        std::string cachePath = cacheFilePathFor(ScenePath, workspace->Name);
        it = m_cache.emplace(
            workspace->Name,
            Pathfinding::NavMesh::Build(
                workspace, currentSettings(), cachePath)).first;
    }

    if (!it->second) return {};
    return it->second->FindPath(start, goal);
}

PathfindingService::PathRequestResult PathfindingService::RequestFindPath(
        Workspace* workspace, const Vector3& start, const Vector3& goal) {
    PathRequestResult result;
    if (!workspace) {
        result.status = RequestStatus::Failed;
        return result;
    }

    finishCompletedBuilds();
    const std::string workspaceKey = workspace->Name;
    const auto cacheIt = m_cache.find(workspaceKey);
    if (cacheIt != m_cache.end()) {
        if (!cacheIt->second) {
            result.status = RequestStatus::Failed;
            return result;
        }
        result.status = RequestStatus::Ready;
        result.waypoints = cacheIt->second->FindPath(start, goal);
        return result;
    }

    RequestId requestId = m_nextRequestId++;
    if (requestId == 0) requestId = m_nextRequestId++;
    auto request = std::make_unique<RequestRecord>();
    request->workspaceKey = workspaceKey;
    request->generation = m_generations[workspaceKey];
    request->start = start;
    request->goal = goal;
    request->status = RequestStatus::Pending;
    result.id = requestId;
    result.status = RequestStatus::Pending;
    m_requests.emplace(requestId, std::move(request));

    const auto buildIt = m_builds.find(workspaceKey);
    if (buildIt == m_builds.end()) {
        startBuild(workspace, workspaceKey, m_generations[workspaceKey]);
    } else if (buildIt->second->generation != m_generations[workspaceKey]) {
        cancelBuild(workspaceKey);
        auto requestIt = m_requests.find(requestId);
        if (requestIt != m_requests.end()) {
            requestIt->second->status = RequestStatus::Pending;
            requestIt->second->generation = m_generations[workspaceKey];
        }
        startBuild(workspace, workspaceKey, m_generations[workspaceKey]);
    }

    // 空のスナップショットなど、ワーカー開始前に確定した失敗を即時反映する。
    const auto requestIt = m_requests.find(requestId);
    if (requestIt != m_requests.end() &&
        requestIt->second->status != RequestStatus::Pending) {
        result.status = requestIt->second->status;
        m_requests.erase(requestIt);
        result.id = 0;
    }
    return result;
}

PathfindingService::RequestStatus PathfindingService::PollRequest(
        RequestId requestId,
        std::vector<Pathfinding::PathWaypoint>& outWaypoints) {
    outWaypoints.clear();
    finishCompletedBuilds();
    const auto requestIt = m_requests.find(requestId);
    if (requestIt == m_requests.end()) return RequestStatus::Unknown;

    RequestRecord& request = *requestIt->second;
    const RequestStatus status = request.status;
    if (status == RequestStatus::Pending) return status;
    if (status == RequestStatus::Ready) {
        outWaypoints = std::move(request.waypoints);
    }
    m_requests.erase(requestIt);
    return status;
}

void PathfindingService::AbandonRequest(RequestId requestId) {
    m_requests.erase(requestId);
}

void PathfindingService::CancelPending() {
    while (!m_builds.empty()) {
        cancelBuild(m_builds.begin()->first);
    }
    for (auto& [requestId, request] : m_requests) {
        if (request->status == RequestStatus::Pending) {
            request->status = RequestStatus::Cancelled;
        }
    }
}

bool PathfindingService::IsBuildActive() {
    if (!s_active) return false;
    for (const auto& [workspaceKey, job] : s_active->m_builds) {
        if (!job->completed.load(std::memory_order_acquire)) return true;
    }
    return false;
}

float PathfindingService::GetBuildProgress() {
    if (!s_active) return 0.0f;
    float progressSum = 0.0f;
    size_t activeCount = 0;
    for (const auto& [workspaceKey, job] : s_active->m_builds) {
        if (job->completed.load(std::memory_order_acquire)) continue;
        progressSum += job->control->progress.load(std::memory_order_acquire);
        ++activeCount;
    }
    return activeCount == 0
        ? 0.0f
        : std::clamp(progressSum / static_cast<float>(activeCount), 0.0f, 1.0f);
}

void PathfindingService::Invalidate(Workspace* workspace) {
    if (!workspace) return;
    finishCompletedBuilds();
    const std::string workspaceKey = workspace->Name;
    m_cache.erase(workspaceKey);
    const uint64_t newGeneration = ++m_generations[workspaceKey];

    bool hasPendingRequests = false;
    const auto buildIt = m_builds.find(workspaceKey);
    if (buildIt != m_builds.end()) {
        buildIt->second->control->cancel();
        if (buildIt->second->worker.joinable()) buildIt->second->worker.join();
        m_builds.erase(buildIt);
    }
    for (auto& [requestId, request] : m_requests) {
        // 完了済みでもまだPollされていない要求は呼び出し側から見れば待機中なので、
        // 編集前のジオメトリで得た結果を捨てて新しい世代へ合流させる。
        if ((request->status == RequestStatus::Pending ||
             request->status == RequestStatus::Ready) &&
            request->workspaceKey == workspaceKey) {
            request->status = RequestStatus::Pending;
            request->waypoints.clear();
            request->generation = newGeneration;
            hasPendingRequests = true;
        }
    }
    if (hasPendingRequests) {
        startBuild(workspace, workspaceKey, newGeneration);
    }
}

void PathfindingService::InvalidateActive(Workspace* workspace) {
    if (s_active) s_active->Invalidate(workspace);
}
