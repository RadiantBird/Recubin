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
    m_clip->space = "model_relative";
}

bool Animation::IsA(std::string className) {
    if (className == "Animation") return true;
    return Instance::IsA(className);
}

void Animation::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Animation", name, value)) return;

    // Scene埋め込みAnimationの旧形式はschema外として読み込み続ける。
    if (name == "Space") {
        if (value.as<std::string>("") == "joint_delta") {
            m_clip->space = "joint_delta"; m_clip->rig = "R6";
        }
        return;
    }
    if (name == "Rig") {
        m_clip->rig = value.as<std::string>("R6");
        return;
    }
    if (name == "Tracks") {
        m_clip->tracks.clear();
        m_source = AnimationSource::LegacyEmbedded;
        m_loadStatus = AnimationClipLoadStatus::Success;
        m_loadMessage.clear();
        for (const auto& trackNode : value) {
            AnimTrack track;
            track.targetKind = m_clip->space == "joint_delta"
                ? AnimationClipTrackTarget::Joint
                : AnimationClipTrackTarget::Part;
            track.targetName = trackNode["PartName"].as<std::string>("");
            const YAML::Node& keys = trackNode["Keyframes"];
            for (const auto& keyNode : keys) {
                Keyframe kf;
                kf.time = keyNode["Time"].as<float>(0.0f);

                const YAML::Node& pos = keyNode["Position"];
                if (pos && pos.size() == 3)
                kf.delta.Position = Vector3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());

                const YAML::Node& rot = keyNode["Rotation"];
                if (rot && rot.size() == 4) // 保存順は [x, y, z, w]
                    kf.delta.Rotation = Quaternion(rot[3].as<float>(), rot[0].as<float>(),
                                                    rot[1].as<float>(), rot[2].as<float>());

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
    m_source = AnimationSource::LegacyEmbedded;
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
        default: return "LegacyEmbedded";
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
    return m_clip->trackFor(partName,
        m_clip->space == "joint_delta" ? AnimationClipTrackTarget::Joint
                                        : AnimationClipTrackTarget::Part);
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
    if (path.size() >= 7) {
        std::string ext = path.substr(path.size() - 7);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (ext == ".rcanim") {
            if (m_clip->space == "joint_delta") return AnimationClipIO::save(path, *m_clip);
            AnimationClip converted; converted.name = Name; converted.length = Length; converted.speed = Speed; converted.looped = Looped;
            for (const auto& legacy : m_clip->tracks) {
                const auto* binding = CharacterRig::findR6Joint(legacy.targetName);
                if (!binding || legacy.keyframes.empty()) {
                    // Legacy names use body-part names rather than joint names.
                    static const std::pair<const char*, const char*> names[] = {
                        {"LeftArm","LeftShoulder"},{"RightArm","RightShoulder"},{"LeftLeg","LeftHip"},{"RightLeg","RightHip"},{"Torso","Torso"},{"Head","Head"}};
                    for (const auto& n : names) if (legacy.targetName == n.first) { binding = CharacterRig::findR6Joint(n.second); break; }
                }
                if (!binding || legacy.keyframes.empty()) return false;
                for (const auto& key : legacy.keyframes)
                    converted.addKey(binding->jointName, key.time,
                        binding->rootToJoint.inverse() * key.delta * binding->jointToPartBind.inverse(), key.easing);
            }
            return AnimationClipIO::save(path, converted);
        }
    }
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Animation" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Length" << YAML::Value << Length;
    out << YAML::Key << "Speed"  << YAML::Value << Speed;
    out << YAML::Key << "Looped" << YAML::Value << Looped;
    out << YAML::Key << "Tracks" << YAML::Value << YAML::BeginSeq;
    for (const AnimTrack& tr : m_clip->tracks) {
        out << YAML::BeginMap;
        out << YAML::Key << "PartName" << YAML::Value << tr.targetName;
        out << YAML::Key << "Keyframes" << YAML::Value << YAML::BeginSeq;
        for (const Keyframe& kf : tr.keyframes) {
            out << YAML::BeginMap;
            out << YAML::Key << "Time" << YAML::Value << kf.time;
            out << YAML::Key << "Position" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << kf.delta.Position.x << kf.delta.Position.y << kf.delta.Position.z
                << YAML::EndSeq;
            out << YAML::Key << "Rotation" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << kf.delta.Rotation.x << kf.delta.Rotation.y
                << kf.delta.Rotation.z << kf.delta.Rotation.w
                << YAML::EndSeq;
            out << YAML::Key << "Easing" << YAML::Value << static_cast<int>(kf.easing);
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap; // Animation
    out << YAML::EndMap;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << out.c_str();
    return ofs.good();
}

bool Animation::importFromFile(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path); // ライブラリ例外は境界で捕捉してリターンコードに変換
    } catch (...) {
        return false;
    }
    if (root["recubin"] && root["recubin"]["type"].as<std::string>("") == "animation") {
        auto result = AnimationClipIO::load(path);
        if (!result) return false;
        m_clip = std::make_unique<AnimationClip>(result.clip);
        ContentPath = path;
        m_source = AnimationSource::File;
        m_loadStatus = AnimationClipLoadStatus::Success;
        m_loadMessage.clear();
        syncClipMetadata();
        return true;
    }
    const YAML::Node& a = root["Animation"];
    if (!a) return false;
    // 既存のsetPropertyのパース処理を再利用する
    if (a["Length"]) setProperty("Length", a["Length"]);
    if (a["Speed"])  setProperty("Speed",  a["Speed"]);
    if (a["Looped"]) setProperty("Looped", a["Looped"]);
    if (a["Tracks"]) setProperty("Tracks", a["Tracks"]);
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
