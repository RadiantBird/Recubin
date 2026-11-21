#pragma once
#include <include/Pathfinding/PathTypes.hpp>
#include <include/Math/Vector3.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

// Instanceツリーをメインスレッドで読み取って作る、ワーカー参照専用の
// ナビメッシュ入力。CaptureGeometry完了後はWorkspaceへアクセスしない。
struct GeometrySnapshot {
    std::vector<float> vertices;
    std::vector<int> triangles;

    bool empty() const;
};

enum class BuildStage : uint8_t {
    Idle,
    ValidatingCache,
    BuildingTiles,
    Finalizing,
    Complete,
    Cancelled,
    Failed,
};

// 非同期構築の進捗と協調キャンセルを呼び出し側と共有する。
struct BuildControl {
    std::atomic<bool> cancelRequested{false};
    std::atomic<BuildStage> stage{BuildStage::Idle};
    std::atomic<uint32_t> completedTiles{0};
    std::atomic<uint32_t> totalTiles{0};
    std::atomic<float> progress{0.0f};

    void cancel();
    bool isCancellationRequested() const;
};

// Workspace内の静的ジオメトリ（Terrainボクセル + 静的BaseCube）から構築した
// Detourナビメッシュ。Workspaceごとにキャッシュして使い回すことを想定する。
class NavMesh {
public:
    ~NavMesh();

    // Instanceツリーを読む処理は必ずメインスレッド側で先に完了させる。
    static GeometrySnapshot CaptureGeometry(Workspace* workspace);

    // 構築に失敗した場合（歩行可能なジオメトリが無い等）は nullptr を返す。
    // cachePath が非空なら、ジオメトリ+設定のハッシュが一致するキャッシュがあれば
    // Recastの再構築をスキップして読み込み、無ければフルビルド後にそこへ保存する。
    static std::unique_ptr<NavMesh> Build(Workspace* workspace, const BuildSettings& settings,
                                           const std::string& cachePath = "");

    // CaptureGeometry済みの不変データだけを使うため、バックグラウンドスレッドから
    // 安全に呼び出せる。controlが指定された場合は進捗とキャンセルを反映する。
    static std::unique_ptr<NavMesh> BuildSnapshot(
        GeometrySnapshot snapshot, const BuildSettings& settings,
        const std::string& cachePath = "", BuildControl* control = nullptr);

    // start/goal に最も近い歩行可能地点間の経路をウェイポイント配列で返す。
    // 経路が見つからない場合は空配列を返す。
    std::vector<PathWaypoint> FindPath(const Vector3& start, const Vector3& goal) const;

private:
    NavMesh() = default;

    dtNavMesh*      m_navMesh  = nullptr;
    dtNavMeshQuery* m_navQuery = nullptr;
};

} // namespace Pathfinding
