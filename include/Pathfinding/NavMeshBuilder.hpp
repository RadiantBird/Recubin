#pragma once
#include <include/Pathfinding/PathTypes.hpp>
#include <include/Math/Vector3.hpp>
#include <memory>
#include <vector>
#include <string>

class Workspace;
class dtNavMesh;
class dtNavMeshQuery;

namespace Pathfinding {

// ナビメッシュ生成・経路探索のエージェント設定
struct BuildSettings {
    float agentRadius     = 1.0f;  // エージェント半径 (stud)
    float agentHeight     = 5.0f;  // エージェント身長 (stud)
    float agentMaxClimb   = 0.6f;  // 段差として登れる最大高さ (stud)
    float agentMaxSlope   = 50.0f; // 歩行可能な最大斜度 (度)
    float maxJumpDistance = 6.0f;  // ジャンプでつなげる境界エッジ間の最大水平距離 (stud)
    float maxJumpHeight   = 4.0f;  // ジャンプでつなげる境界エッジ間の最大高低差 (stud)
};

// Workspace内の静的ジオメトリ（Terrainボクセル + 静的BaseCube）から構築した
// Detourナビメッシュ。Workspaceごとにキャッシュして使い回すことを想定する。
class NavMesh {
public:
    ~NavMesh();

    // 構築に失敗した場合（歩行可能なジオメトリが無い等）は nullptr を返す。
    // cachePath が非空なら、ジオメトリ+設定のハッシュが一致するキャッシュがあれば
    // Recastの再構築をスキップして読み込み、無ければフルビルド後にそこへ保存する。
    static std::unique_ptr<NavMesh> Build(Workspace* workspace, const BuildSettings& settings,
                                           const std::string& cachePath = "");

    // start/goal に最も近い歩行可能地点間の経路をウェイポイント配列で返す。
    // 経路が見つからない場合は空配列を返す。
    std::vector<PathWaypoint> FindPath(const Vector3& start, const Vector3& goal) const;

private:
    NavMesh() = default;

    dtNavMesh*      m_navMesh  = nullptr;
    dtNavMeshQuery* m_navQuery = nullptr;
};

} // namespace Pathfinding
