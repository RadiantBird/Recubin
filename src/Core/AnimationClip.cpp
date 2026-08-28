#include <Core/AnimationClip.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <Core/CharacterRig.hpp>

static bool finite(float v) { return std::isfinite(v); }
static EasingType parseEasing(const YAML::Node& n, bool& ok) {
    std::string s = n.as<std::string>("linear");
    if (s == "linear") return EasingType::Linear;
    if (s == "quadratic") return EasingType::Quadratic;
    if (s == "cosine") return EasingType::Cosine;
    if (s == "sine") return EasingType::Sine;
    if (s == "exponential") return EasingType::Exponential;
    ok = false; return EasingType::Linear;
}

CFrame AnimationClip::evaluate(const AnimationClipTrack& track, float t) const {
    if (track.keyframes.empty()) return CFrame();
    if (t <= track.keyframes.front().time) return track.keyframes.front().delta;
    if (t >= track.keyframes.back().time) return track.keyframes.back().delta;
    auto it = std::lower_bound(track.keyframes.begin(), track.keyframes.end(), t,
        [](const AnimationClipKeyframe& k, float v) { return k.time < v; });
    const auto& b = *it; const auto& a = *(it - 1);
    float span = b.time - a.time;
    float u = span > 1e-6f ? (t - a.time) / span : 0.0f;
    u = ease(a.easing, u);
    return CFrame(a.delta.Position + (b.delta.Position - a.delta.Position) * u,
                  Quaternion::Slerp(a.delta.Rotation, b.delta.Rotation, u));
}

const AnimationClipTrack* AnimationClip::findTrack(const std::string& targetName) const {
    for (const auto& t : tracks) if (t.targetName == targetName) return &t;
    return nullptr;
}
AnimationClipTrack& AnimationClip::trackFor(const std::string& targetName,
                                            AnimationClipTrackTarget targetKind) {
    for (auto& t : tracks) if (t.targetName == targetName && t.targetKind == targetKind) return t;
    tracks.push_back({});
    tracks.back().targetKind = targetKind;
    tracks.back().targetName = targetName;
    return tracks.back();
}
void AnimationClip::addKey(const std::string& targetName, float time, const CFrame& delta, EasingType easing) {
    const AnimationClipTrackTarget targetKind =
        space == "model_relative" ? AnimationClipTrackTarget::Part : AnimationClipTrackTarget::Joint;
    auto& t = trackFor(targetName, targetKind);
    for (auto& k : t.keyframes) if (std::fabs(k.time - time) < 1e-5f) { k = {time, delta, easing}; return; }
    t.keyframes.push_back({time, delta, easing});
    std::sort(t.keyframes.begin(), t.keyframes.end(), [](auto& a, auto& b) { return a.time < b.time; });
}

AnimationClip AnimationClip::defaultR6Walk() {
    AnimationClip c; c.name = "R6Walk"; c.length = 2.0f / 3.0f; c.speed = 1.0f; c.looped = true;
    for (auto [joint, sign] : {std::pair<const char*, float>{"LeftShoulder", 1.0f}, {"RightShoulder", -1.0f},
                               {"LeftHip", -1.0f}, {"RightHip", 1.0f}}) {
        c.addKey(joint, 0.0f, CFrame::fromAxisAngle(Vector3(1,0,0), 0.0f));
        c.addKey(joint, c.length * 0.25f, CFrame::fromAxisAngle(Vector3(1,0,0), sign * 35.0f));
        c.addKey(joint, c.length * 0.5f, CFrame::fromAxisAngle(Vector3(1,0,0), 0.0f));
        c.addKey(joint, c.length * 0.75f, CFrame::fromAxisAngle(Vector3(1,0,0), sign * -35.0f));
        c.addKey(joint, c.length, CFrame::fromAxisAngle(Vector3(1,0,0), 0.0f));
    }
    return c;
}

AnimationClipLoadResult AnimationClipIO::load(const std::string& path) {
    AnimationClipLoadResult r;
    std::ifstream test(path, std::ios::binary); if (!test) {
        r.status = std::filesystem::exists(path) ? AnimationClipLoadStatus::IOError : AnimationClipLoadStatus::NotFound;
        r.message = r.status == AnimationClipLoadStatus::NotFound ? "file not found" : "unable to open file"; return r;
    }
    try {
        YAML::Node root = YAML::LoadFile(path), h = root["recubin"];
        if (!h || h["type"].as<std::string>("") != "animation") { 
            r.status = AnimationClipLoadStatus::TypeMismatch;
            r.message = "not an animation"; return r;
        }
        if (h["version"].as<int>(-1) != 1) {
            r.status = AnimationClipLoadStatus::UnsupportedVersion;
            r.message = "unsupported animation version: " + std::to_string(h["version"].as<int>(-1));
            return r;
        }
        auto a = root["animation"];
        if (!a || !a.IsMap()) {
            r.status = AnimationClipLoadStatus::InvalidData;
            r.message = "animation must be a map"; return r;
        }
        AnimationClip c;
        c.name = a["name"].as<std::string>("");
        c.rig = a["rig"].as<std::string>("");
        c.space = a["space"].as<std::string>("");
        c.length = a["length"].as<float>(0);
        c.speed = a["speed"].as<float>(1);
        c.looped = a["looped"].as<bool>(false);
        if (c.rig != "R6" || c.space != "joint_delta" || !finite(c.length) || c.length <= 0 || !finite(c.speed)) {
            r.status = AnimationClipLoadStatus::InvalidData;
            r.message = "invalid animation metadata"; return r;
        }
        if (!a["tracks"] || !a["tracks"].IsSequence() || a["tracks"].size() == 0) {
            r.status = AnimationClipLoadStatus::InvalidData;
            r.message = "tracks must be a non-empty sequence";
            return r;
        }
        for (const auto& tn : a["tracks"]) {
            if (!tn.IsMap()) { r.status = AnimationClipLoadStatus::InvalidData; r.message = "track must be a map"; return r; }
            std::string joint = tn["joint"].as<std::string>(""); if (joint.empty() || !CharacterRig::findR6Joint(joint)) { r.status = AnimationClipLoadStatus::InvalidData; r.message = "unknown or empty joint"; return r; }
            if (c.findTrack(joint) || !tn["keyframes"] || !tn["keyframes"].IsSequence() || tn["keyframes"].size() == 0) { r.status = AnimationClipLoadStatus::InvalidData; r.message = "duplicate joint or empty keyframes"; return r; }
            for (const auto& kn : tn["keyframes"]) {
                float t = kn["time"].as<float>(-1); auto p = kn["position"]; auto q = kn["rotation"]; bool ok = true;
                if (!finite(t) || t < 0 || t > c.length) { r.status=AnimationClipLoadStatus::InvalidData; r.message="keyframe time out of range"; return r; }
                if (!p || p.size()!=3 || !q || q.size()!=4) { r.status=AnimationClipLoadStatus::InvalidData; r.message="invalid keyframe position or rotation shape"; return r; }
                Vector3 pos(p[0].as<float>(),p[1].as<float>(),p[2].as<float>()); Quaternion rot(q[3].as<float>(),q[0].as<float>(),q[1].as<float>(),q[2].as<float>());
                for (float v : {pos.x,pos.y,pos.z,rot.x,rot.y,rot.z,rot.w}) if (!finite(v)) { r.status=AnimationClipLoadStatus::InvalidData; r.message="non-finite keyframe value"; return r; }
                const float norm = std::sqrt(rot.w*rot.w + rot.x*rot.x + rot.y*rot.y + rot.z*rot.z);
                if (norm < 1e-5f || std::fabs(norm - 1.0f) > 0.01f) { r.status=AnimationClipLoadStatus::InvalidData; r.message="invalid quaternion"; return r; }
                c.addKey(joint,t,CFrame(pos,rot),parseEasing(kn["easing"],ok)); if (!ok) { r.status=AnimationClipLoadStatus::InvalidData; r.message="unknown easing"; return r; }
            }
        }
        r.status=AnimationClipLoadStatus::Success; r.clip=std::move(c); return r;
    } catch (const YAML::BadFile& e) { r.status=AnimationClipLoadStatus::IOError; r.message=e.what(); return r;
    } catch (const YAML::BadConversion& e) { r.status=AnimationClipLoadStatus::InvalidData; r.message=e.what(); return r;
    } catch (const YAML::ParserException& e) { r.status=AnimationClipLoadStatus::InvalidYaml; r.message=e.what(); return r;
    } catch (const YAML::Exception& e) { r.status=AnimationClipLoadStatus::InvalidYaml; r.message=e.what(); return r;
    } catch (const std::exception& e) { r.status=AnimationClipLoadStatus::InvalidData; r.message=e.what(); return r; }
}

bool AnimationClipIO::save(const std::string& path, const AnimationClip& c) {
    try { YAML::Emitter o; o << YAML::BeginMap << YAML::Key << "recubin" << YAML::Value << YAML::BeginMap << YAML::Key << "type" << YAML::Value << "animation" << YAML::Key << "version" << YAML::Value << 1 << YAML::EndMap << YAML::Key << "animation" << YAML::Value << YAML::BeginMap;
        o << YAML::Key << "name" << YAML::Value << c.name << YAML::Key << "rig" << YAML::Value << c.rig << YAML::Key << "space" << YAML::Value << c.space << YAML::Key << "length" << YAML::Value << c.length << YAML::Key << "speed" << YAML::Value << c.speed << YAML::Key << "looped" << YAML::Value << c.looped << YAML::Key << "tracks" << YAML::Value << YAML::BeginSeq;
        auto easingName = [](EasingType e) { switch (e) { case EasingType::Quadratic:return "quadratic"; case EasingType::Cosine:return "cosine"; case EasingType::Sine:return "sine"; case EasingType::Exponential:return "exponential"; default:return "linear"; } };
        for (const auto& t:c.tracks) { if (t.targetKind != AnimationClipTrackTarget::Joint || t.targetName.empty()) return false; o << YAML::BeginMap << YAML::Key << "joint" << YAML::Value << t.targetName << YAML::Key << "keyframes" << YAML::Value << YAML::BeginSeq; for (const auto& k:t.keyframes) o << YAML::BeginMap << YAML::Key << "time" << YAML::Value << k.time << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginSeq << k.delta.Position.x << k.delta.Position.y << k.delta.Position.z << YAML::EndSeq << YAML::Key << "rotation" << YAML::Value << YAML::Flow << YAML::BeginSeq << k.delta.Rotation.x << k.delta.Rotation.y << k.delta.Rotation.z << k.delta.Rotation.w << YAML::EndSeq << YAML::Key << "easing" << YAML::Value << easingName(k.easing) << YAML::EndMap; o << YAML::EndSeq << YAML::EndMap; } o << YAML::EndSeq << YAML::EndMap << YAML::EndMap; std::ofstream f(path); if(!f) return false; f << o.c_str(); return f.good(); } catch (...) { return false; }
}
