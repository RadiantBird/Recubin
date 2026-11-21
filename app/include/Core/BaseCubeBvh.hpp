#pragma once
#include <Math/Vector3.hpp>

#include <cstdint>
#include <functional>
#include <vector>

class BaseCube;
class Workspace;

// Workspace内のBaseCubeのワールド境界(AABB)に対する境界階層(BVH)。選択用のレイキャストが
// 全Cubeを線形に調べずに済むよう、光線が当たりうるCubeだけを近い順にたどる。
// 境界は保守的(OBBを必ず含む)で、厳密な当たり判定は呼び出し側のvisitorが行う。
//
// 無効化: Workspaceのツリー変更(getTreeRevision)と、姿勢/Sizeの変更
// (Spatial::boundsEpoch)のどちらかが変わると古くなる。動いている間は構築せず、
// 2回続けて変化が無かったときだけ構築する(prepare)。
class BaseCubeBvh {
public:
    // 現在のWorkspaceに対して使える状態にできたらtrue。falseのときは、呼び出し側が
    // 従来どおりの線形走査にフォールバックする。構築が走る場合がある(1回限りの負荷)。
    bool prepare(const Workspace& workspace);

    // visitor(cube, order, bestDistance): 光線に当たりうるCubeごとに、近い順に近似して
    // 呼ばれる。厳密に当たったら bestDistance を更新する。order は構築時のCubeの並び
    // (getRenderBaseCubes)でのindex。同距離の優先順位を線形走査と揃えるのに使う。
    // bestDistance より遠いノードは枝刈りされる。
    using Visitor = std::function<void(BaseCube* cube, std::uint32_t order, float& bestDistance)>;
    void traverseRay(const Vector3& origin, const Vector3& direction,
                     float& bestDistance, const Visitor& visitor) const;

    bool isBuilt() const { return m_built; }
    std::size_t itemCount() const { return m_items.size(); }

private:
    struct Item {
        float minimum[3];
        float maximum[3];
        BaseCube* cube;
        std::uint32_t order;
    };
    struct Node {
        float minimum[3];
        float maximum[3];
        // 内部ノード(count==0): 左の子は自身の次、右の子はsecond。
        // 葉(count>0): second は m_items の開始位置。
        std::uint32_t second;
        std::uint32_t count;
    };

    void build(const Workspace& workspace);
    std::uint32_t buildRange(std::uint32_t begin, std::uint32_t end);

    std::vector<Item> m_items;
    std::vector<Node> m_nodes;
    bool m_built = false;
    // 構築時の状態。
    std::uint64_t m_builtTreeRevision = 0;
    std::uint64_t m_builtBoundsEpoch = 0;
    const Workspace* m_builtWorkspace = nullptr;
    // 直前のprepare()で見た状態。2回続けて同じなら「安定した」とみなして構築する。
    std::uint64_t m_seenTreeRevision = 0;
    std::uint64_t m_seenBoundsEpoch = 0;
    bool m_hasSeen = false;
};
