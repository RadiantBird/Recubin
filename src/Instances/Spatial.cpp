#include "include/Instances/Spatial.hpp"
#include <algorithm>
#include <vector>

namespace {
struct DescendantPose {
    Spatial* target;
    CFrame world;
    std::size_t depth;
};

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

CFrame Spatial::getWorldCFrame() const {
    if (auto* coordinateParent = getCoordinateParent()) {
        // 親もワールド CFrame を持つ → 親ワールド * 自ローカル で合成
        return coordinateParent->getWorldCFrame() * m_cframe;
    }
    return m_cframe; // Workspace 直下 or 親なし → ローカル = ワールド
}

Spatial* Spatial::getCoordinateParent() const {
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

void Spatial::setRotation(const Quaternion& value) {
    Quaternion normalized = value;
    if (!normalized.tryNormalize()) return;
    CFrame valueFrame = m_cframe;
    valueFrame.Rotation = normalized;
    setCFrame(valueFrame);
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
    if (name == "Position" || name == "Size") {
        if (value.IsSequence() && value.size() == 3) {
            Vector3 vec;
            vec.x = value[0].as<float>();
            vec.y = value[1].as<float>();
            vec.z = value[2].as<float>();
            if (name == "Position") this->setPosition(vec);
            else                     this->Size     = vec;
        }
    } else if (name == "Rotation") {
        // [x, y, z, w] 形式で保存された Quaternion を読み込む
        if (value.IsSequence() && value.size() == 4) {
            Quaternion rotation;
            if (Quaternion::tryFromComponents(value[3].as<float>(), value[0].as<float>(),
                                               value[1].as<float>(), value[2].as<float>(), rotation))
                setRotation(rotation);
        }
    } else {
        Instance::setProperty(name, value);
    }
}
