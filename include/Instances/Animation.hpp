#pragma once

#include <Instances/Instance.hpp>
#include <Math/CFrame.hpp>
#include <Math/Easing.hpp>
#include <Core/AnimationClip.hpp>
#include <string>
#include <memory>

// ==================================================================
//  Animation
//
//  Scene Tree上のアニメーション資産。AnimationClipが唯一のトラック
//  データで、AnimTrack/Keyframeは旧APIの型aliasとしてのみ残す。
// ==================================================================

using Keyframe = AnimationClipKeyframe;
using AnimTrack = AnimationClipTrack;

enum class AnimationSource {
    LegacyEmbedded,
    File,
    BuiltIn
};

class Animation : public Instance {
public:
    float Length = 1.0f;   // 全長（秒）
    float Speed  = 1.0f;   // 再生速度倍率
    bool  Looped = false;  // trueでループ再生
    std::string ContentPath;

    Animation();

    std::string getClassName() override { return "Animation"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    // 指定時刻tにおけるトラックのCFrameを補間して返す。範囲外はクランプ。
    CFrame evaluateTrack(const AnimTrack& track, float t) const;

    // partNameのトラックの時刻timeにキーを追加（同時刻が存在すれば上書き）
    void addOrReplaceKey(const std::string& partName, float time,
                         const CFrame& cframe, EasingType easing);
    // partNameのトラックの時刻timeに最も近いキーを削除
    void removeKey(const std::string& partName, float time);

    std::vector<AnimTrack>& getTracks() { return m_clip->tracks; }
    const std::vector<AnimTrack>& getTracks() const { return m_clip->tracks; }
    AnimationClip* getClip() { return m_clip.get(); }
    const AnimationClip* getClip() const { return m_clip.get(); }
    void setClip(const AnimationClip& clip);
    void setClip(std::shared_ptr<AnimationClip> clip);
    void syncClipMetadata();

    bool loadContent();
    AnimationSource getSource() const { return m_source; }
    AnimationClipLoadStatus getLoadStatus() const { return m_loadStatus; }
    std::string getSourceName() const;
    std::string getLoadStatusName() const;
    const std::string& getLoadMessage() const { return m_loadMessage; }
    bool isUsingBuiltInFallback() const { return m_usingBuiltInFallback; }
    void setUsingBuiltInFallback(bool value) const { m_usingBuiltInFallback = value; }
    void setBuiltInClip(const AnimationClip& clip);
    // Walk用に使用可能なR6 joint_delta Clipを返す。読込失敗時も
    // 保存参照は書き換えず、この実行中だけ内蔵Clipを返す。
    const AnimationClip& resolveR6WalkClip() const;

    // 単体の .yaml ファイルへ書き出す/読み込む（シーンとは独立した再利用用）。
    // 成功時 true を返す（失敗はリターンコードで扱い、例外は投げない）。
    bool exportToFile(const std::string& path) const;
    bool importFromFile(const std::string& path);

private:
    std::unique_ptr<AnimationClip> m_clip;
    AnimationSource m_source = AnimationSource::LegacyEmbedded;
    AnimationClipLoadStatus m_loadStatus = AnimationClipLoadStatus::Success;
    std::string m_loadMessage;
    mutable bool m_usingBuiltInFallback = false;

    // partNameのトラックを返す（なければ作成）
    AnimTrack& trackFor(const std::string& partName);
};
