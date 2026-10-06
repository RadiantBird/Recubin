#include <include/Instances/Model.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
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

bool isDescendantOf(const Instance& root, const Instance& node) {
    for (auto parent = node.Parent.lock(); parent; parent = parent->Parent.lock()) {
        if (parent.get() == &root) return true;
    }
    return false;
}

const bool s_modelRegistered = [] {
    using namespace PropertyRegistry;
    PropertyDesc primaryCube = custom("PrimaryCube", PropType::String,
        [](Instance* instance) {
            return PropValue(static_cast<Model*>(instance)->getPrimaryCubePath());
        },
        [](Instance* instance, const PropValue& value) {
            static_cast<Model*>(instance)->setPrimaryCubePath(std::get<std::string>(value));
        });
    primaryCube.omitEmpty();
    primaryCube.instanceRefClass = "BaseCube";
    primaryCube.editorWidget = EditorWidget::InstanceReference;
    registerClass("Model", "Spatial", {primaryCube});
    return true;
}();
}

std::shared_ptr<BaseCube> Model::resolvePrimaryCube(const std::string& path) const {
    if (path.empty()) return nullptr;
    // 解決の起点はTool::resolveHandle/getWorkspaceRelativePathの規約と一致させる
    auto* self = const_cast<Model*>(this);
    Instance* base = self->findFirstAncestorWorkspace();
    if (!base) {
        base = self;
        for (auto p = Parent.lock(); p; p = p->Parent.lock()) base = p.get();
    }
    Instance* found = base->getChildByPath(path);
    if (!found || !found->IsA("BaseCube")) return nullptr;
    return std::static_pointer_cast<BaseCube>(found->shared_from_this());
}

BaseCube* Model::getPrimaryCube() const {
    std::shared_ptr<BaseCube> cube = m_primaryCube.lock();
    if (!cube) {
        cube = resolvePrimaryCube(m_primaryCubeName);
        if (!cube) return nullptr;
        m_primaryCube = cube;
    }
    if (!isDescendantOf(*this, *cube)) {
        if (!m_primaryCubeWarned) {
            RCBN_ERROR("Model '" << Name << "' PrimaryCube '" << cube->Name
                       << "' is not a descendant of the Model; ignored");
            m_primaryCubeWarned = true;
        }
        return nullptr;
    }
    return cube.get();
}

std::string Model::getPrimaryCubePath() const {
    // リネーム/リペアレントでパスが古くならないよう、解決済みなら生きている参照から作り直す
    if (const BaseCube* cube = getPrimaryCube())
        m_primaryCubeName = const_cast<BaseCube*>(cube)->getWorkspaceRelativePath();
    return m_primaryCubeName;
}

void Model::setPrimaryCubePath(const std::string& path) {
    std::shared_ptr<BaseCube> cube = resolvePrimaryCube(path);
    if (cube && !isDescendantOf(*this, *cube)) {
        RCBN_ERROR("Model '" << Name << "' PrimaryCube must be a descendant of the Model: "
                   << path);
        return;
    }
    // 解決できない場合(読み込み中で子がまだ無い等)はパスだけ保持し、後で遅延解決する
    m_primaryCubeName = path;
    m_primaryCube = cube;
    m_primaryCubeWarned = false;
    if (cube) syncPivotToPrimaryCube();
}

void Model::setPrimaryCube(const std::shared_ptr<BaseCube>& cube) {
    m_primaryCube = cube;
    m_primaryCubeName = cube ? cube->getWorkspaceRelativePath() : std::string{};
    m_primaryCubeWarned = false;
    syncPivotToPrimaryCube();
}

void Model::collectInstanceReferences(std::vector<InstanceReference>& out) {
    getPrimaryCube();
    out.push_back({m_primaryCube.lock(), "BaseCube", "Model.PrimaryCube",
        [this](std::shared_ptr<Instance> v) { setPrimaryCube(std::dynamic_pointer_cast<BaseCube>(v)); }});
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
    if (const BaseCube* primary = getPrimaryCube())
        return CFrame(primary->getWorldPosition(), getWorldCFrame().Rotation);

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

namespace {
constexpr float PIVOT_SYNC_EPSILON = 0.01f;
// PrimaryCubeはPositionの表示値になるため、重心(近似)より厳しく追従する
constexpr float PRIMARY_PIVOT_SYNC_EPSILON = 1.0e-4f;

bool hasDynamicCube(const Instance& root) {
    for (const auto& [_, child] : root.children) {
        if (!child) continue;
        if (const auto* cube = dynamic_cast<const BaseCube*>(child.get());
            cube && !cube->Anchored)
            return true;
        if (hasDynamicCube(*child)) return true;
    }
    return false;
}
}

void Model::syncPivotToPrimaryCube() {
    const BaseCube* primary = getPrimaryCube();
    if (!primary || IsA("Tool")) return;
    const CFrame world = getWorldCFrame();
    const Vector3 target = primary->getWorldPosition();
    if ((target - world.Position).length() < PRIMARY_PIVOT_SYNC_EPSILON) return;
    // setWorldCFrameは子孫のワールド姿勢を保存する。物理ボディは動かさない。
    setWorldCFrame(CFrame(target, world.Rotation));
}

void Model::syncPivotsToPrimaryCube(const std::vector<Model*>& models) {
    for (Model* model : models) {
        if (model) model->syncPivotToPrimaryCube();
    }
}

void Model::syncPivotToCentroid() {
    if (getPrimaryCube()) {
        syncPivotToPrimaryCube();
        return;
    }
    if (IsA("Tool") || !hasDynamicCube(*this)) return;
    const CFrame world = getWorldCFrame();
    const CFrame pivot = getPivotCFrame();
    if ((pivot.Position - world.Position).length() < PIVOT_SYNC_EPSILON) return;
    // setWorldCFrameは子孫のワールド姿勢を保存する。物理ボディは動かさない。
    setWorldCFrame(CFrame(pivot.Position, world.Rotation));
}

void Model::syncPivotsToCentroid(Instance& root) {
    for (const auto& [_, child] : root.children) {
        if (!child) continue;
        if (auto* model = dynamic_cast<Model*>(child.get()))
            model->syncPivotToCentroid();
        syncPivotsToCentroid(*child);
    }
}

void Model::syncPivotsToCentroid(const std::vector<Model*>& models) {
    for (Model* model : models) {
        if (model) model->syncPivotToCentroid();
    }
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
    auto copy = std::make_shared<Model>();
    cloneInto(*copy);
    return copy;
}

void Model::cloneInto(Model& copy) const {
    copy.Name = Name;
    // 派生クラス(StarterCharacter等)は固有のスキーマを持たないため、IsAで適用される
    // Model/Spatialのスキーマで複製する。
    PropertyRegistry::copyCompatibleProperties(this, &copy);
    // ModelのPosition/Rotation代入は子孫ごと剛体移動(pivotTo)する意味論で、子の無いcopyでは
    // 原点がずれる。姿勢はローカルCFrameをそのまま代入して確定させる。
    copy.setCFrame(getCFrame());

    for (auto const& [name, child] : children) {
        copy.addChild(child->clone());
    }

    // 複製先の子孫へ張り替える(元のCubeを指したままにしない)
    if (BaseCube* primary = getPrimaryCube()) {
        const std::string relative = primary->getPathUpTo(const_cast<Model*>(this));
        Instance* target = copy.getChildByPath(relative);
        if (target && target->IsA("BaseCube"))
            copy.setPrimaryCube(std::static_pointer_cast<BaseCube>(target->shared_from_this()));
    }
}
