#include "Core/BaseCubeBvh.hpp"

#include "Instances/BaseCube.hpp"
#include "Instances/Skybox.hpp"
#include "Instances/Spatial.hpp"
#include "Instances/Workspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr std::uint32_t MAX_LEAF_ITEMS = 4;
// 浮動小数の誤差で、厳密な判定では当たるCubeを境界で落とさないための余白。
constexpr float BOUNDS_PADDING = 1e-3f;
constexpr float INF = std::numeric_limits<float>::infinity();

struct PreparedRay {
    float origin[3];
    float inverse[3];
    bool parallel[3];
};

PreparedRay prepareRay(const Vector3& origin, const Vector3& direction) {
    PreparedRay ray{};
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {direction.x, direction.y, direction.z};
    for (int axis = 0; axis < 3; ++axis) {
        ray.origin[axis] = o[axis];
        ray.parallel[axis] = std::abs(d[axis]) < 1e-12f;
        ray.inverse[axis] = ray.parallel[axis] ? 0.0f : 1.0f / d[axis];
    }
    return ray;
}

// 光線(t>=0)がAABBと交わるなら、入る距離をtEnterに返す(内側から始まる場合は0)。
bool rayHitsBox(const PreparedRay& ray, const float minimum[3], const float maximum[3],
                float maxDistance, float& tEnter) {
    float tNear = 0.0f;
    float tFar = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        if (ray.parallel[axis]) {
            if (ray.origin[axis] < minimum[axis] || ray.origin[axis] > maximum[axis]) return false;
            continue;
        }
        float t1 = (minimum[axis] - ray.origin[axis]) * ray.inverse[axis];
        float t2 = (maximum[axis] - ray.origin[axis]) * ray.inverse[axis];
        if (t1 > t2) std::swap(t1, t2);
        tNear = std::max(tNear, t1);
        tFar = std::min(tFar, t2);
        if (tNear > tFar) return false;
    }
    tEnter = tNear;
    return true;
}
}

bool BaseCubeBvh::prepare(const Workspace& workspace) {
    const std::uint64_t treeRevision = workspace.getTreeRevision();
    const std::uint64_t boundsEpoch = Spatial::boundsEpoch();

    if (m_built && m_builtWorkspace == &workspace &&
        m_builtTreeRevision == treeRevision && m_builtBoundsEpoch == boundsEpoch) {
        return true;
    }

    // 前回の問い合わせから何も変わっていない(=動き続けてはいない)ときだけ構築する。
    // 物理やギズモのドラッグで毎フレーム変わる間は、構築を繰り返さず線形走査に任せる。
    const bool stable = m_hasSeen && m_seenTreeRevision == treeRevision &&
        m_seenBoundsEpoch == boundsEpoch;
    m_seenTreeRevision = treeRevision;
    m_seenBoundsEpoch = boundsEpoch;
    m_hasSeen = true;
    if (!stable) return false;

    build(workspace);
    return true;
}

void BaseCubeBvh::build(const Workspace& workspace) {
    m_items.clear();
    m_nodes.clear();

    const auto& cubes = workspace.getRenderBaseCubes();
    const auto& skyboxes = workspace.getRenderSkyboxes();
    m_items.reserve(cubes.size());
    for (std::size_t index = 0; index < cubes.size(); ++index) {
        BaseCube* cube = cubes[index];
        if (!cube) continue;
        // Skyboxは選択対象外(線形走査と同じ)。
        if (!skyboxes.empty() &&
            std::find(skyboxes.begin(), skyboxes.end(), cube) != skyboxes.end()) {
            continue;
        }

        const CFrame world = cube->getWorldCFrame();
        const Vector3 half = cube->Size * 0.5f;
        // 回転したOBBを含む軸並行AABB: 各軸の半径 = sum(|R列の成分| * 半サイズ)
        const Vector3 axisX = world.Rotation.rotate(Vector3(1.0f, 0.0f, 0.0f));
        const Vector3 axisY = world.Rotation.rotate(Vector3(0.0f, 1.0f, 0.0f));
        const Vector3 axisZ = world.Rotation.rotate(Vector3(0.0f, 0.0f, 1.0f));
        const float extent[3] = {
            std::abs(axisX.x) * half.x + std::abs(axisY.x) * half.y + std::abs(axisZ.x) * half.z,
            std::abs(axisX.y) * half.x + std::abs(axisY.y) * half.y + std::abs(axisZ.y) * half.z,
            std::abs(axisX.z) * half.x + std::abs(axisY.z) * half.y + std::abs(axisZ.z) * half.z,
        };
        const float center[3] = {world.Position.x, world.Position.y, world.Position.z};

        Item item{};
        for (int axis = 0; axis < 3; ++axis) {
            item.minimum[axis] = center[axis] - extent[axis] - BOUNDS_PADDING;
            item.maximum[axis] = center[axis] + extent[axis] + BOUNDS_PADDING;
        }
        // NaNや無限大の境界は階層を壊すので、選択対象から外す(厳密判定でも当たらない)。
        bool finite = true;
        for (int axis = 0; axis < 3; ++axis) {
            finite = finite && std::isfinite(item.minimum[axis]) && std::isfinite(item.maximum[axis]);
        }
        if (!finite) continue;
        item.cube = cube;
        item.order = static_cast<std::uint32_t>(index);
        m_items.push_back(item);
    }

    if (!m_items.empty()) {
        m_nodes.reserve(m_items.size() / 2 + 1);
        buildRange(0, static_cast<std::uint32_t>(m_items.size()));
    }

    m_built = true;
    m_builtWorkspace = &workspace;
    m_builtTreeRevision = workspace.getTreeRevision();
    m_builtBoundsEpoch = Spatial::boundsEpoch();
}

std::uint32_t BaseCubeBvh::buildRange(std::uint32_t begin, std::uint32_t end) {
    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(m_nodes.size());
    m_nodes.push_back({});

    float minimum[3] = {INF, INF, INF};
    float maximum[3] = {-INF, -INF, -INF};
    float centroidMin[3] = {INF, INF, INF};
    float centroidMax[3] = {-INF, -INF, -INF};
    for (std::uint32_t i = begin; i < end; ++i) {
        const Item& item = m_items[i];
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], item.minimum[axis]);
            maximum[axis] = std::max(maximum[axis], item.maximum[axis]);
            const float centroid = 0.5f * (item.minimum[axis] + item.maximum[axis]);
            centroidMin[axis] = std::min(centroidMin[axis], centroid);
            centroidMax[axis] = std::max(centroidMax[axis], centroid);
        }
    }
    Node node{};
    for (int axis = 0; axis < 3; ++axis) {
        node.minimum[axis] = minimum[axis];
        node.maximum[axis] = maximum[axis];
    }

    if (end - begin <= MAX_LEAF_ITEMS) {
        node.second = begin;
        node.count = end - begin;
        m_nodes[nodeIndex] = node;
        return nodeIndex;
    }

    // 重心の広がりが最も大きい軸で、中央値分割する。
    int splitAxis = 0;
    float widest = centroidMax[0] - centroidMin[0];
    for (int axis = 1; axis < 3; ++axis) {
        const float width = centroidMax[axis] - centroidMin[axis];
        if (width > widest) {
            widest = width;
            splitAxis = axis;
        }
    }
    const std::uint32_t middle = begin + (end - begin) / 2;
    std::nth_element(
        m_items.begin() + begin, m_items.begin() + middle, m_items.begin() + end,
        [splitAxis](const Item& a, const Item& b) {
            return a.minimum[splitAxis] + a.maximum[splitAxis] <
                   b.minimum[splitAxis] + b.maximum[splitAxis];
        });

    buildRange(begin, middle);                          // 左の子 = nodeIndex + 1
    node.second = buildRange(middle, end);              // 右の子
    node.count = 0;
    m_nodes[nodeIndex] = node;
    return nodeIndex;
}

void BaseCubeBvh::traverseRay(const Vector3& origin, const Vector3& direction,
                              float& bestDistance, const Visitor& visitor) const {
    if (m_nodes.empty()) return;
    const PreparedRay ray = prepareRay(origin, direction);

    struct StackEntry {
        std::uint32_t node;
        float enter;
    };
    // 深さは約log2(N/4)。余裕を持たせた固定長スタック。
    StackEntry stack[96];
    int top = 0;

    float rootEnter = 0.0f;
    if (!rayHitsBox(ray, m_nodes[0].minimum, m_nodes[0].maximum, bestDistance, rootEnter)) return;
    stack[top++] = {0, rootEnter};

    while (top > 0) {
        const StackEntry entry = stack[--top];
        if (entry.enter > bestDistance) continue;
        const Node& node = m_nodes[entry.node];

        if (node.count > 0) {
            for (std::uint32_t i = node.second; i < node.second + node.count; ++i) {
                const Item& item = m_items[i];
                float itemEnter = 0.0f;
                if (!rayHitsBox(ray, item.minimum, item.maximum, bestDistance, itemEnter)) continue;
                visitor(item.cube, item.order, bestDistance);
            }
            continue;
        }

        const std::uint32_t leftIndex = entry.node + 1;
        const std::uint32_t rightIndex = node.second;
        float leftEnter = 0.0f;
        float rightEnter = 0.0f;
        const bool hitLeft = rayHitsBox(
            ray, m_nodes[leftIndex].minimum, m_nodes[leftIndex].maximum, bestDistance, leftEnter);
        const bool hitRight = rayHitsBox(
            ray, m_nodes[rightIndex].minimum, m_nodes[rightIndex].maximum, bestDistance, rightEnter);
        // 近いほうを先に処理するため、遠いほうを先にpushする。
        if (hitLeft && hitRight) {
            if (leftEnter <= rightEnter) {
                stack[top++] = {rightIndex, rightEnter};
                stack[top++] = {leftIndex, leftEnter};
            } else {
                stack[top++] = {leftIndex, leftEnter};
                stack[top++] = {rightIndex, rightEnter};
            }
        } else if (hitLeft) {
            stack[top++] = {leftIndex, leftEnter};
        } else if (hitRight) {
            stack[top++] = {rightIndex, rightEnter};
        }
    }
}
