#include <include/Instances/Model.hpp>
#include <include/Core/Physics.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Weld.hpp>
#include <include/Util/Logger.hpp>
#include <atomic>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

namespace {
std::atomic<std::uint32_t> s_nextCharacterCollisionGroup{1};

std::uint32_t allocateCharacterCollisionGroup() {
    constexpr std::uint32_t MAX_GROUP =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    std::uint32_t next =
        s_nextCharacterCollisionGroup.load(std::memory_order_relaxed);
    while (next <= MAX_GROUP) {
        if (s_nextCharacterCollisionGroup.compare_exchange_weak(
                next, next + 1, std::memory_order_relaxed))
            return next;
    }
    RCBN_ERROR("Character collision group ID space exhausted");
    return 0;
}

struct PivotPose {
    Spatial* target;
    CFrame world;
    std::size_t depth;
};

void collectPivotPoses(Instance& root, std::vector<PivotPose>& out,
                       std::size_t depth = 1) {
    for (const auto& [_, child] : root.children) {
        if (!child) continue;
        if (auto* spatial = dynamic_cast<Spatial*>(child.get()))
            out.push_back({spatial, spatial->getWorldCFrame(), depth});
        collectPivotPoses(*child, out, depth + 1);
    }
}
}

void Model::refreshCharacterCollisionGroup() {
    bool hasDirectHumanoid = false;
    for (const auto& [_, child] : children) {
        if (child && child->IsA("Humanoid")) {
            hasDirectHumanoid = true;
            break;
        }
    }

    const std::uint32_t oldGroup = m_characterCollisionGroup;
    if (hasDirectHumanoid && m_characterCollisionGroup == 0)
        m_characterCollisionGroup = allocateCharacterCollisionGroup();
    else if (!hasDirectHumanoid)
        m_characterCollisionGroup = 0;

    if (oldGroup == m_characterCollisionGroup) return;
    for (const auto& [_, child] : children) {
        if (child) child->onAncestorChanged();
    }
}

CFrame Model::getPivotCFrame() const {
    Vector3 sumPositions(0, 0, 0);
    std::size_t count = 0;

    std::vector<PivotPose> descendants;
    collectPivotPoses(const_cast<Model&>(*this), descendants);

    for (const auto& pose : descendants) {
        if (pose.target && pose.target->IsA("BaseCube")) {
            sumPositions = sumPositions + pose.world.Position;
            ++count;
        }
    }

    if (count == 0) {
        for (const auto& pose : descendants) {
            if (pose.target) {
                sumPositions = sumPositions + pose.world.Position;
                ++count;
            }
        }
    }

    const CFrame modelWorld = getWorldCFrame();
    if (count == 0) {
        return modelWorld;
    }

    const Vector3 centroid = sumPositions * (1.0f / static_cast<float>(count));
    return CFrame(centroid, modelWorld.Rotation);
}

void Model::pivotTo(const CFrame& worldCFrame) {
    CFrame normalized = worldCFrame;
    if (!normalized.Rotation.tryNormalize()) return;
    const CFrame oldPivotWorld = getPivotCFrame();
    const CFrame delta = normalized * oldPivotWorld.inverse();
    std::vector<PivotPose> descendants;
    collectPivotPoses(*this, descendants);
    std::stable_sort(descendants.begin(), descendants.end(),
        [](const PivotPose& a, const PivotPose& b) { return a.depth < b.depth; });
    Workspace* workspace = dynamic_cast<Workspace*>(findFirstAncestorWorkspace());
    Physics* physics = workspace ? workspace->getPhysicsEngine() : nullptr;

    const CFrame newModelWorld = delta * getWorldCFrame();
    if (auto* parent = getCoordinateParent())
        commitCFrame(parent->getWorldCFrame().inverse() * newModelWorld,
                     SpatialUpdateOrigin::Deserialization);
    else
        commitCFrame(newModelWorld, SpatialUpdateOrigin::Deserialization);

    // Physics-backed members keep their native body as the authoritative world pose,
    // so we sync those bodies directly after the model's graph transform has moved.
    // Each welded assembly must be moved exactly once using its primary/first member pose.
    std::unordered_set<const BaseCube*> processedCubes;
    for (const auto& pose : descendants) {
        const CFrame targetWorld = delta * pose.world;
        auto cube = std::dynamic_pointer_cast<BaseCube>(
            pose.target->shared_from_this());
        if (physics && cube && physics->hasBody(*cube)) {
            if (processedCubes.contains(cube.get())) continue;

            std::vector<std::shared_ptr<BaseCube>> assembly{cube};
            if (workspace)
                assembly = Weld::collectAssembly(cube, *workspace);
            for (const auto& member : assembly) {
                if (member) processedCubes.insert(member.get());
            }

            physics->moveWeldAssembly(cube, targetWorld);
        }
    }
}

void Model::onChildrenChanged() {
    refreshCharacterCollisionGroup();
    Instance::onChildrenChanged();
}

std::shared_ptr<Instance> Model::clone() const {
    auto copy = std::make_shared<Model>(getPosition(), Size);
    copy->Name = Name;
    copy->setCFrame(getCFrame());

    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }

    return copy;
}
