#include <Instances/Animation.hpp>
#include <Core/CharacterRig.hpp>
#include <Instances/Model.hpp>
#include <Math/Quaternion.hpp>
#include <Core/PropertyRegistry.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cctype>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>

static const bool s_animationRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Animation", "Instance", {
        method_prop<&Animation::getLength, &Animation::setLength>("Length"),
        method_prop<&Animation::getSpeed, &Animation::setSpeed>("Speed"),
        method_prop<&Animation::getLooped, &Animation::setLooped>("Looped"),
        method_prop<&Animation::getContentPath, &Animation::setContentPath>("ContentPath")
            .omitEmpty()
            .filePath("Recubin Animation (*.rcanim)", "*.rcanim")
            .noClone(),
    });
    return true;
}();

Animation::Animation() : Instance("Animation"), m_clip(std::make_unique<AnimationClip>()) {
    m_clip->rig = "R6";
    m_clip->space = "joint_delta";
}

bool Animation::IsA(std::string className) {
    if (className == "Animation") return true;
    return Instance::IsA(className);
}

void Animation::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Animation", name, value)) return;

    if (name == "Space") {
        m_clip->space = "joint_delta";
        m_clip->rig = "R6";
        return;
    }
    if (name == "Rig") {
        m_clip->rig = "R6";
        return;
    }
    if (name == "Tracks") {
        m_clip->tracks.clear();
        m_source = AnimationSource::Embedded;
        m_loadStatus = AnimationClipLoadStatus::Success;
        m_loadMessage.clear();
        for (const auto& trackNode : value) {
            AnimTrack track;
            track.targetKind = AnimationClipTrackTarget::Joint;
            track.targetName = trackNode["JointName"].as<std::string>("");
            const YAML::Node& keys = trackNode["Keyframes"];
            for (const auto& keyNode : keys) {
                Keyframe kf;
                kf.time = keyNode["Time"].as<float>(0.0f);

                const YAML::Node& pos = keyNode["Position"];
                if (pos && pos.size() == 3)
                kf.delta.Position = Vector3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());

                const YAML::Node& rot = keyNode["Rotation"];
                if (rot && rot.size() == 4) { // 保存順は [x, y, z, w]
                    const float lenSq = rot[0].as<float>() * rot[0].as<float>() +
                        rot[1].as<float>() * rot[1].as<float>() +
                        rot[2].as<float>() * rot[2].as<float>() +
                        rot[3].as<float>() * rot[3].as<float>();
                    if (std::isfinite(lenSq) && std::abs(lenSq - 1.0f) > 1e-4f)
                        RCBN_WARN("normalizing legacy Animation Rotation (lengthSquared=" << lenSq << ")");
                    Quaternion rotation;
                    if (Quaternion::tryFromComponents(rot[3].as<float>(), rot[0].as<float>(),
                                                       rot[1].as<float>(), rot[2].as<float>(), rotation))
                        kf.delta.Rotation = rotation;
                }

                kf.easing = static_cast<EasingType>(keyNode["Easing"].as<int>(0));
                track.keyframes.push_back(kf);
            }
            std::sort(track.keyframes.begin(), track.keyframes.end(),
                      [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
            m_clip->tracks.push_back(std::move(track));
        }
        m_clip->length = Length; m_clip->speed = Speed; m_clip->looped = Looped;
        return;
    }
    Instance::setProperty(name, value);
}

void Animation::setLength(float value) {
    Length = value;
    if (m_clip) m_clip->length = value;
}

void Animation::setSpeed(float value) {
    Speed = value;
    if (m_clip) m_clip->speed = value;
}

void Animation::setLooped(bool value) {
    Looped = value;
    if (m_clip) m_clip->looped = value;
}

void Animation::setContentPath(const std::string& value) {
    ContentPath = value;
    loadContent();
}

void Animation::syncClipMetadata() {
    Length = m_clip->length; Speed = m_clip->speed; Looped = m_clip->looped;
}

void Animation::setClip(const AnimationClip& clip) {
    m_clip = std::make_unique<AnimationClip>(clip);
    m_source = AnimationSource::Embedded;
    m_loadStatus = AnimationClipLoadStatus::Success;
    m_loadMessage.clear();
    m_usingBuiltInFallback = false;
    syncClipMetadata();
}

void Animation::setClip(std::shared_ptr<AnimationClip> clip) {
    if (clip) setClip(*clip);
}

const AnimationClip& Animation::resolveR6WalkClip() const {
    static const AnimationClip builtin = AnimationClip::defaultR6Walk();
    const bool valid = m_loadStatus == AnimationClipLoadStatus::Success && m_clip &&
                       m_clip->rig == "R6" && m_clip->space == "joint_delta";
    m_usingBuiltInFallback = !valid;
    return valid ? *m_clip : builtin;
}

bool Animation::loadContent() {
    m_source = AnimationSource::File;
    m_usingBuiltInFallback = false;
    if (ContentPath.empty()) {
        m_loadStatus = AnimationClipLoadStatus::NotFound;
        m_loadMessage = "ContentPath is empty";
        return false;
    }
    const auto result = AnimationClipIO::load(AssetPath::normalize(ContentPath));
    m_loadStatus = result.status;
    m_loadMessage = result.message;
    if (!result) return false;
    m_clip = std::make_unique<AnimationClip>(result.clip);
    syncClipMetadata();
    return true;
}

std::string Animation::getSourceName() const {
    switch (m_source) {
        case AnimationSource::File: return "File";
        case AnimationSource::BuiltIn: return "BuiltIn";
        default: return "Embedded";
    }
}

std::string Animation::getLoadStatusName() const {
    switch (m_loadStatus) {
        case AnimationClipLoadStatus::Success: return "Success";
        case AnimationClipLoadStatus::NotFound: return "NotFound";
        case AnimationClipLoadStatus::IOError: return "IOError";
        case AnimationClipLoadStatus::InvalidYaml: return "InvalidYaml";
        case AnimationClipLoadStatus::TypeMismatch: return "TypeMismatch";
        case AnimationClipLoadStatus::UnsupportedVersion: return "UnsupportedVersion";
        default: return "InvalidData";
    }
}

void Animation::setBuiltInClip(const AnimationClip& clip) {
    m_clip = std::make_unique<AnimationClip>(clip);
    ContentPath.clear();
    m_source = AnimationSource::BuiltIn;
    m_loadStatus = AnimationClipLoadStatus::Success;
    m_loadMessage.clear();
    m_usingBuiltInFallback = false;
    syncClipMetadata();
}

std::shared_ptr<Instance> Animation::clone() const {
    auto copy = std::make_shared<Animation>();
    copy->Name = Name;
    copy->m_clip = std::make_unique<AnimationClip>(*m_clip);
    PropertyRegistry::cloneFields(this, copy.get(), "Animation");
    // ContentPathはnoClone。setter経由で外部ファイルを再読込せず、
    // 読込済み/失敗中のClipと保存参照をそのまま引き継ぐ。
    copy->ContentPath = ContentPath;
    copy->m_source = m_source;
    copy->m_loadStatus = m_loadStatus;
    copy->m_loadMessage = m_loadMessage;
    copy->m_usingBuiltInFallback = m_usingBuiltInFallback;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

CFrame Animation::evaluateTrack(const AnimTrack& track, float t) const {
    const auto& keys = track.keyframes;
    if (keys.empty()) return CFrame();
    return m_clip->evaluate(track, t);
}

AnimTrack& Animation::trackFor(const std::string& partName) {
    return m_clip->trackFor(partName, AnimationClipTrackTarget::Joint);
}

void Animation::addOrReplaceKey(const std::string& partName, float time,
                                const CFrame& cframe, EasingType easing) {
    AnimTrack& track = trackFor(partName);
    for (auto& kf : track.keyframes) {
        if (std::fabs(kf.time - time) < 1e-4f) {
            kf.delta = cframe;
            kf.easing = easing;
            return;
        }
    }
    Keyframe kf;
    kf.time   = time;
    kf.delta = cframe;
    kf.easing = easing;
    track.keyframes.push_back(kf);
    std::sort(track.keyframes.begin(), track.keyframes.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

bool Animation::exportToFile(const std::string& path) const {
    if (!m_clip || m_clip->rig != "R6" || m_clip->space != "joint_delta") return false;
    if (path.size() < 7 || path.substr(path.size() - 7) != ".rcanim") return false;
    return AnimationClipIO::save(path, *m_clip);
}

bool Animation::importFromFile(const std::string& path) {
    const auto result = AnimationClipIO::load(path);
    if (!result || result.clip.rig != "R6" || result.clip.space != "joint_delta") return false;
    m_clip = std::make_unique<AnimationClip>(result.clip);
    ContentPath = path;
    m_source = AnimationSource::File;
    m_loadStatus = AnimationClipLoadStatus::Success;
    m_loadMessage.clear();
    syncClipMetadata();
    return true;
}

void Animation::removeKey(const std::string& partName, float time) {
    for (auto& tr : m_clip->tracks) {
        if (tr.targetName != partName) continue;
        auto& keys = tr.keyframes;
        if (keys.empty()) return;
        size_t best = 0;
        float bestDist = std::fabs(keys[0].time - time);
        for (size_t i = 1; i < keys.size(); ++i) {
            float d = std::fabs(keys[i].time - time);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        keys.erase(keys.begin() + best);
        return;
    }
}
