#include <Instances/Animation.hpp>
#include <Math/Quaternion.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <fstream>

Animation::Animation() : Instance("Animation") {}

bool Animation::IsA(std::string className) {
    if (className == "Animation") return true;
    return Instance::IsA(className);
}

void Animation::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Length") { Length = value.as<float>(); return; }
    if (name == "Speed")  { Speed  = value.as<float>(); return; }
    if (name == "Tracks") {
        m_tracks.clear();
        for (const auto& trackNode : value) {
            AnimTrack track;
            track.partName = trackNode["PartName"].as<std::string>("");
            const YAML::Node& keys = trackNode["Keyframes"];
            for (const auto& keyNode : keys) {
                Keyframe kf;
                kf.time = keyNode["Time"].as<float>(0.0f);

                const YAML::Node& pos = keyNode["Position"];
                if (pos && pos.size() == 3)
                    kf.cframe.Position = Vector3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());

                const YAML::Node& rot = keyNode["Rotation"];
                if (rot && rot.size() == 4) // 保存順は [x, y, z, w]
                    kf.cframe.Rotation = Quaternion(rot[3].as<float>(), rot[0].as<float>(),
                                                    rot[1].as<float>(), rot[2].as<float>());

                kf.easing = static_cast<EasingType>(keyNode["Easing"].as<int>(0));
                track.keyframes.push_back(kf);
            }
            std::sort(track.keyframes.begin(), track.keyframes.end(),
                      [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
            m_tracks.push_back(std::move(track));
        }
        return;
    }
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> Animation::clone() const {
    auto copy = std::make_shared<Animation>();
    copy->Name    = Name;
    copy->Length  = Length;
    copy->Speed   = Speed;
    copy->m_tracks = m_tracks;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

CFrame Animation::evaluateTrack(const AnimTrack& track, float t) const {
    const auto& keys = track.keyframes;
    if (keys.empty()) return CFrame();
    if (t <= keys.front().time) return keys.front().cframe;
    if (t >= keys.back().time)  return keys.back().cframe;

    // tを囲む2キーを探す
    size_t next = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time >= t) { next = i; break; }
    }
    const Keyframe& a = keys[next - 1];
    const Keyframe& b = keys[next];

    float span = b.time - a.time;
    float u = (span > 1e-6f) ? (t - a.time) / span : 0.0f;
    u = ease(a.easing, u); // 開始キー側のeasingを区間に適用

    CFrame result;
    result.Position = a.cframe.Position + (b.cframe.Position - a.cframe.Position) * u;
    result.Rotation = Quaternion::Slerp(a.cframe.Rotation, b.cframe.Rotation, u);
    return result;
}

AnimTrack& Animation::trackFor(const std::string& partName) {
    for (auto& tr : m_tracks) {
        if (tr.partName == partName) return tr;
    }
    AnimTrack tr;
    tr.partName = partName;
    m_tracks.push_back(std::move(tr));
    return m_tracks.back();
}

void Animation::addOrReplaceKey(const std::string& partName, float time,
                                const CFrame& cframe, EasingType easing) {
    AnimTrack& track = trackFor(partName);
    for (auto& kf : track.keyframes) {
        if (std::fabs(kf.time - time) < 1e-4f) {
            kf.cframe = cframe;
            kf.easing = easing;
            return;
        }
    }
    Keyframe kf;
    kf.time   = time;
    kf.cframe = cframe;
    kf.easing = easing;
    track.keyframes.push_back(kf);
    std::sort(track.keyframes.begin(), track.keyframes.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

bool Animation::exportToFile(const std::string& path) const {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Animation" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Length" << YAML::Value << Length;
    out << YAML::Key << "Speed"  << YAML::Value << Speed;
    out << YAML::Key << "Tracks" << YAML::Value << YAML::BeginSeq;
    for (const AnimTrack& tr : m_tracks) {
        out << YAML::BeginMap;
        out << YAML::Key << "PartName" << YAML::Value << tr.partName;
        out << YAML::Key << "Keyframes" << YAML::Value << YAML::BeginSeq;
        for (const Keyframe& kf : tr.keyframes) {
            out << YAML::BeginMap;
            out << YAML::Key << "Time" << YAML::Value << kf.time;
            out << YAML::Key << "Position" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << kf.cframe.Position.x << kf.cframe.Position.y << kf.cframe.Position.z
                << YAML::EndSeq;
            out << YAML::Key << "Rotation" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << kf.cframe.Rotation.x << kf.cframe.Rotation.y
                << kf.cframe.Rotation.z << kf.cframe.Rotation.w
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
    const YAML::Node& a = root["Animation"];
    if (!a) return false;
    // 既存のsetPropertyのパース処理を再利用する
    if (a["Length"]) setProperty("Length", a["Length"]);
    if (a["Speed"])  setProperty("Speed",  a["Speed"]);
    if (a["Tracks"]) setProperty("Tracks", a["Tracks"]);
    return true;
}

void Animation::removeKey(const std::string& partName, float time) {
    for (auto& tr : m_tracks) {
        if (tr.partName != partName) continue;
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
