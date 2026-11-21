#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Pathfinding/NavMeshBuilder.hpp>
#include <include/Math/Vector3.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class Workspace;

// ==================================================================
//  PathfindingService
//
//  System直下に自動生成される独立したサービス。Workspace内のTerrain
//  ボクセル + 静的BaseCubeジオメトリからDetourナビメッシュを構築し、
//  2点間の経路をウェイポイント配列(Vector3 + Walk/Jump)として返す。
//  ナビメッシュはWorkspaceごとにキャッシュし、地形編集時にInvalidate
//  される（TerrainStreamer::applyBrush参照）。
// ==================================================================
class PathfindingService : public Instance {
public:
    using RequestId = uint64_t;

    enum class RequestStatus {
        Unknown,
        Pending,
        Ready,
        Failed,
        Cancelled,
    };

    struct PathRequestResult {
        RequestId id = 0;
        RequestStatus status = RequestStatus::Unknown;
        std::vector<Pathfinding::PathWaypoint> waypoints;
    };

    float AgentRadius     = 1.0f;
    float AgentHeight     = 5.0f;
    float AgentMaxClimb   = 0.6f;
    float AgentMaxSlope   = 50.0f;
    float MaxJumpDistance = 6.0f;
    float MaxJumpHeight   = 4.0f;

    // シーンファイルのパス。SceneRuntime::loadAndBind がシーン読み込みのたびに設定する。
    // ディスクキャッシュファイルのパス算出に使う（未設定ならディスクキャッシュしない）。
    std::string ScenePath;

    PathfindingService();
    ~PathfindingService() override;

    std::string getClassName() override { return "PathfindingService"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;

    // start/goalに最も近い歩行可能地点間の経路を返す。workspaceが未キャッシュなら
    // 現在のAgent系プロパティでナビメッシュを構築してからキャッシュする。
    std::vector<Pathfinding::PathWaypoint> FindPath(Workspace* workspace, const Vector3& start, const Vector3& goal);

    // キャッシュヒット時はReadyと経路を即時返す。キャッシュミス時は同じWorkspaceの
    // 構築へ集約し、Pendingと要求IDを返す。
    PathRequestResult RequestFindPath(
        Workspace* workspace, const Vector3& start, const Vector3& goal);

    // Ready/Failed/Cancelledになった要求は、この呼び出しで結果を返して破棄する。
    RequestStatus PollRequest(
        RequestId requestId, std::vector<Pathfinding::PathWaypoint>& outWaypoints);

    // 呼び出し側がyieldできない場合など、結果を待たない要求だけを破棄する。
    // 同じWorkspaceで共有中のナビメッシュ構築は継続する。
    void AbandonRequest(RequestId requestId);

    // 全非同期構築を協調キャンセルし、ワーカー終了を待つ。
    void CancelPending();

    // エディター/ランタイム共通の待機UIから参照するグローバル状態。
    static bool IsBuildActive();
    static float GetBuildProgress();

    // 指定Workspaceのナビメッシュキャッシュを破棄する（次回FindPathで再構築される）。
    void Invalidate(Workspace* workspace);

    // TerrainStreamerなどInstanceツリーを辿れない箇所から、現在アクティブな
    // PathfindingServiceのキャッシュを無効化するための静的ヘルパー。
    static void InvalidateActive(Workspace* workspace);

private:
    struct BuildJob;
    struct RequestRecord;

    Pathfinding::BuildSettings currentSettings() const;
    void startBuild(
        Workspace* workspace, const std::string& workspaceKey, uint64_t generation);
    void finishCompletedBuilds();
    void cancelBuild(const std::string& workspaceKey);

    // Workspace* ではなく Workspace 名でキーイングする。Workspace は Play/Stop の
    // たびに破棄・再生成されるため、ポインタキーだと毎回キャッシュミスする上、
    // アドレス再利用時に別Workspaceへ誤ヒットする恐れがある。
    std::unordered_map<std::string, std::unique_ptr<Pathfinding::NavMesh>> m_cache;
    std::unordered_map<std::string, std::unique_ptr<BuildJob>> m_builds;
    std::unordered_map<RequestId, std::unique_ptr<RequestRecord>> m_requests;
    std::unordered_map<std::string, uint64_t> m_generations;
    RequestId m_nextRequestId = 1;

    static PathfindingService* s_active;
};
