#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Pathfinding/NavMeshBuilder.hpp>
#include <include/Math/Vector3.hpp>
#include <unordered_map>
#include <memory>

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
    float AgentRadius     = 1.0f;
    float AgentHeight     = 5.0f;
    float AgentMaxClimb   = 0.6f;
    float AgentMaxSlope   = 50.0f;
    float MaxJumpDistance = 6.0f;
    float MaxJumpHeight   = 4.0f;

    PathfindingService();
    ~PathfindingService() override;

    std::string getClassName() override { return "PathfindingService"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;

    // start/goalに最も近い歩行可能地点間の経路を返す。workspaceが未キャッシュなら
    // 現在のAgent系プロパティでナビメッシュを構築してからキャッシュする。
    std::vector<Pathfinding::PathWaypoint> FindPath(Workspace* workspace, const Vector3& start, const Vector3& goal);

    // 指定Workspaceのナビメッシュキャッシュを破棄する（次回FindPathで再構築される）。
    void Invalidate(Workspace* workspace);

    // TerrainStreamerなどInstanceツリーを辿れない箇所から、現在アクティブな
    // PathfindingServiceのキャッシュを無効化するための静的ヘルパー。
    static void InvalidateActive(Workspace* workspace);

private:
    std::unordered_map<Workspace*, std::unique_ptr<Pathfinding::NavMesh>> m_cache;

    static PathfindingService* s_active;
};
