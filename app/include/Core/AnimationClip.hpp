#pragma once

#include <Math/CFrame.hpp>
#include <Math/Easing.hpp>
#include <string>
#include <vector>

// Runtime representation shared by built-in and serialized animations.  Track
// values are local deltas from a rig binding (space == joint_delta).
struct AnimationClipKeyframe {
    float time = 0.0f;
    CFrame delta;
    EasingType easing = EasingType::Linear;
};

enum class AnimationClipTrackTarget {
    Joint,
    Part
};

struct AnimationClipTrack {
    AnimationClipTrackTarget targetKind = AnimationClipTrackTarget::Joint;
    std::string targetName;
    std::vector<AnimationClipKeyframe> keyframes;
};

class AnimationClip {
public:
    std::string name = "";
    std::string rig = "R6";
    std::string space = "joint_delta";
    float length = 1.0f;
    float speed = 1.0f;
    bool looped = false;
    std::vector<AnimationClipTrack> tracks;

    CFrame evaluate(const AnimationClipTrack& track, float time) const;
    const AnimationClipTrack* findTrack(const std::string& targetName) const;
    AnimationClipTrack& trackFor(const std::string& targetName,
                                 AnimationClipTrackTarget targetKind = AnimationClipTrackTarget::Joint);
    void addKey(const std::string& targetName, float time, const CFrame& delta,
                EasingType easing = EasingType::Linear);

    static AnimationClip defaultR6Walk();
};

enum class AnimationClipLoadStatus {
    Success, NotFound, IOError, InvalidYaml, TypeMismatch,
    UnsupportedVersion, InvalidData
};

struct AnimationClipLoadResult {
    AnimationClipLoadStatus status = AnimationClipLoadStatus::InvalidData;
    AnimationClip clip;
    std::string message;
    explicit operator bool() const { return status == AnimationClipLoadStatus::Success; }
};

class AnimationClipIO {
public:
    static AnimationClipLoadResult load(const std::string& path);
    static bool save(const std::string& path, const AnimationClip& clip);
};
