#include "include/Instances/Spatial.hpp"
#include "include/Core/PropertyRegistry.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/Instances/Model.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <functional>
#include <cmath>
#include <vector>

namespace {
// プロパティ(エディター/YAML)経由のローカルCFrame代入。Modelは子孫を
// 一緒に動かす(ギズモと同じ挙動)。それ以外は従来のsetterに委ねる。
bool isPlainModel(Instance* object) {
    return object->IsA("Model") && !object->IsA("Tool");
}

void assignLocalCFrameProperty(Spatial* spatial, const CFrame& local) {
    if (!spatial->IsA("Model")) {
        spatial->setCFrame(local);
        return;
    }
    if (!spatial->IsA("Tool")) {
        // Modelのプロパティ代入はPivotToと同じ。原点が指定姿勢になるよう、
        // 子孫を含めて剛体的に移動する(物理ボディも同期される)。
        auto* model = static_cast<Model*>(spatial);
        CFrame target = local;
        if (!target.Rotation.tryNormalize()) return;
        if (const Spatial* parent = model->getCoordinateParent())
            target = parent->getWorldCFrame() * target;
        const CFrame delta = target * model->getWorldCFrame().inverse();
        model->pivotTo(delta * model->getPivotCFrame());
        return;
    }
    CFrame normalized = local;
    if (!normalized.Rotation.tryNormalize()) return;
    spatial->commitCFrame(normalized, Spatial::SpatialUpdateOrigin::Editor);
    std::vector<BaseCube*> cubes;
    std::function<void(Instance&)> collect = [&](Instance& root) {
        for (const auto& [_, child] : root.children) {
            if (!child) continue;
            if (auto* cube = dynamic_cast<BaseCube*>(child.get())) cubes.push_back(cube);
            collect(*child);
        }
    };
    collect(*spatial);
    for (BaseCube* cube : cubes) cube->teleportTo(cube->getPosition());
}

const bool s_spatialRegistered = [] {
    using namespace PropertyRegistry;
    PropertyDesc position = custom("Position", PropType::Vec3,
            [](Instance* object) {
                return PropValue(static_cast<Spatial*>(object)->getPosition());
            },
            [](Instance* object, const PropValue& value) {
                auto* spatial = static_cast<Spatial*>(object);
                CFrame local = spatial->getCFrame();
                local.Position = std::get<Vector3>(value);
                if (spatial->IsA("Model")) assignLocalCFrameProperty(spatial, local);
                else spatial->setPosition(std::get<Vector3>(value));
            });
    position.luaSet = [position](Instance* object, const PropValue& value) {
        if (isPlainModel(object)) position.set(object, value);
        else static_cast<Spatial*>(object)->setPosition(std::get<Vector3>(value));
    };
    position.lo = -1.0e9f;
    position.hi = 1.0e9f;
    position.step = 0.05f;
    position.group("Spatial");

    PropertyDesc size = custom("Size", PropType::Vec3,
            [](Instance* object) {
                return PropValue(static_cast<Spatial*>(object)->Size);
            },
            [](Instance* object, const PropValue& value) {
                static_cast<Spatial*>(object)->setSize(std::get<Vector3>(value));
            });
    size.lo = 0.001f;
    size.hi = 1.0e6f;
    size.step = 0.05f;

    PropertyDesc rotation = custom("Rotation", PropType::Quaternion,
            [](Instance* object) {
                return PropValue(static_cast<Spatial*>(object)->getRotation());
            },
            [](Instance* object, const PropValue& value) {
                auto* spatial = static_cast<Spatial*>(object);
                CFrame local = spatial->getCFrame();
                local.Rotation = std::get<Quaternion>(value);
                if (spatial->IsA("Model")) assignLocalCFrameProperty(spatial, local);
                else spatial->setRotation(std::get<Quaternion>(value));
            });
    rotation.luaSet = [rotation](Instance* object, const PropValue& value) {
        if (isPlainModel(object)) rotation.set(object, value);
        else static_cast<Spatial*>(object)->setRotation(std::get<Quaternion>(value));
    };
    PropertyDesc cframe = custom("CFrame", PropType::CFrame,
            [](Instance* object) {
                return PropValue(static_cast<Spatial*>(object)->getCFrame());
            },
            [](Instance* object, const PropValue& value) {
                assignLocalCFrameProperty(static_cast<Spatial*>(object), std::get<CFrame>(value));
            });
    cframe.luaSet = [cframe](Instance* object, const PropValue& value) {
        if (isPlainModel(object)) cframe.set(object, value);
        else static_cast<Spatial*>(object)->setCFrame(std::get<CFrame>(value));
    };
    cframe.noYaml().multiOnly();
    registerClass("Spatial", "Instance", {position, size, rotation, cframe});
    return true;
}();

struct DescendantPose {
    Spatial* target;
    CFrame world;
    std::size_t depth;
};

// 値が完全に同じ書き込みでboundsEpochを進めない(Skyboxは毎フレームカメラ位置へ
// 書き込むが、カメラが止まっていれば空間インデックスを無効化したくない)。
bool sameFrameExact(const CFrame& a, const CFrame& b) {
    return a.Position == b.Position &&
        a.Rotation.w == b.Rotation.w && a.Rotation.x == b.Rotation.x &&
        a.Rotation.y == b.Rotation.y && a.Rotation.z == b.Rotation.z;
}

void collectDescendantPoses(Instance& root, std::vector<DescendantPose>& out,
                            std::size_t depth = 1) {
    for (const auto& [_, child] : root.children) {
        if (!child) continue;
        if (auto* spatial = dynamic_cast<Spatial*>(child.get()))
            out.push_back({spatial, spatial->getWorldCFrame(), depth});
        collectDescendantPoses(*child, out, depth + 1);
    }
}

void assignWorldCFrame(Spatial& target, const CFrame& world) {
    if (auto* parent = target.getCoordinateParent())
        target.commitCFrame(parent->getWorldCFrame().inverse() * world,
                            Spatial::SpatialUpdateOrigin::Deserialization);
    else
        target.commitCFrame(world, Spatial::SpatialUpdateOrigin::Deserialization);
}
}

void Spatial::refreshHierarchyCache() {
    auto parent = Parent.lock();
    while (parent) {
        if (dynamic_cast<Spatial*>(parent.get())) {
            m_hasSpatialAncestor = true;
            return;
        }
        parent = parent->Parent.lock();
    }
    m_hasSpatialAncestor = false;
}

CFrame Spatial::getWorldCFrame() const {
    if (!m_hasSpatialAncestor) return m_cframe;
    if (auto* coordinateParent = getCoordinateParent()) {
        // 親もワールド CFrame を持つ → 親ワールド * 自ローカル で合成
        return coordinateParent->getWorldCFrame() * m_cframe;
    }
    return m_cframe; // Workspace 直下 or 親なし → ローカル = ワールド
}

Spatial* Spatial::getCoordinateParent() const {
    if (!m_hasSpatialAncestor) return nullptr;
    auto parent = Parent.lock();
    while (parent) {
        if (auto* spatial = dynamic_cast<Spatial*>(parent.get())) return spatial;
        parent = parent->Parent.lock();
    }
    return nullptr;
}

void Spatial::setWorldCFrame(const CFrame& worldCFrame) {
    CFrame normalized = worldCFrame;
    if (!normalized.Rotation.tryNormalize()) return;
    std::vector<DescendantPose> descendants;
    collectDescendantPoses(*this, descendants);
    assignWorldCFrame(*this, normalized);
    std::stable_sort(descendants.begin(), descendants.end(),
        [](const DescendantPose& a, const DescendantPose& b) { return a.depth < b.depth; });
    for (const auto& pose : descendants) assignWorldCFrame(*pose.target, pose.world);
}

void Spatial::setCFrame(const CFrame& value) {
    CFrame normalized = value;
    if (!normalized.Rotation.tryNormalize()) return;
    std::vector<DescendantPose> descendants;
    collectDescendantPoses(*this, descendants);
    if (!sameFrameExact(m_cframe, normalized)) notifyBoundsChanged();
    m_cframe = normalized;
    std::stable_sort(descendants.begin(), descendants.end(),
        [](const DescendantPose& a, const DescendantPose& b) { return a.depth < b.depth; });
    for (const auto& pose : descendants) assignWorldCFrame(*pose.target, pose.world);
}

void Spatial::commitCFrame(const CFrame& value, SpatialUpdateOrigin origin) {
    CFrame normalized = value;
    if (!normalized.Rotation.tryNormalize()) return;
    std::vector<DescendantPose> descendants;
    if (origin == SpatialUpdateOrigin::Physics || origin == SpatialUpdateOrigin::Network)
        collectDescendantPoses(*this, descendants);
    if (!sameFrameExact(m_cframe, normalized)) notifyBoundsChanged();
    m_cframe = normalized;
    if (!descendants.empty()) {
        std::stable_sort(descendants.begin(), descendants.end(),
            [](const DescendantPose& a, const DescendantPose& b) { return a.depth < b.depth; });
        for (const auto& pose : descendants) assignWorldCFrame(*pose.target, pose.world);
    }
}

void Spatial::setPosition(const Vector3& value) {
    CFrame valueFrame = m_cframe;
    valueFrame.Position = value;
    setCFrame(valueFrame);
}

void Spatial::setWorldPosition(const Vector3& worldPosition) {
    if (!std::isfinite(worldPosition.x) || !std::isfinite(worldPosition.y) ||
        !std::isfinite(worldPosition.z)) {
        RCBN_ERROR("Rejected invalid WorldPosition for " << getClassName()
                   << " " << getFullPath() << ": [" << worldPosition.x
                   << "," << worldPosition.y << "," << worldPosition.z << "]");
        return;
    }

    CFrame world = getWorldCFrame();
    world.Position = worldPosition;
    CFrame local = world;
    if (auto* coordinateParent = getCoordinateParent())
        local = coordinateParent->getWorldCFrame().inverse() * world;
    if (isPlainModel(this)) {
        // Modelは代入をPivotToと同じ扱いにする(プロパティ代入と揃える)
        CFrame newLocal = getCFrame();
        newLocal.Position = local.Position;
        assignLocalCFrameProperty(this, newLocal);
        return;
    }
    setPosition(local.Position);
}

void Spatial::setRotation(const Quaternion& value) {
    Quaternion normalized = value;
    if (!normalized.tryNormalize()) return;
    CFrame valueFrame = m_cframe;
    valueFrame.Rotation = normalized;
    setCFrame(valueFrame);
}

void Spatial::setSize(const Vector3& value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z) || value.x <= 0.0f || value.y <= 0.0f ||
        value.z <= 0.0f) {
        return;
    }
    if (!(Size == value)) notifyBoundsChanged();
    Size = value;
}

void Spatial::applyLocalCFrameBatch(
    const std::vector<std::pair<Spatial*, CFrame>>& values) {
    for (const auto& [target, value] : values) {
        if (!target) continue;
        CFrame normalized = value;
        if (!normalized.Rotation.tryNormalize()) continue;
        target->commitCFrame(normalized, SpatialUpdateOrigin::Deserialization);
    }
}

bool Spatial::IsA(std::string className) {
    if (className == "Spatial") {
        return true;
    }
    return Instance::IsA(className);
}

void Spatial::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Spatial", name, value)) return;
    Instance::setProperty(name, value);
}
