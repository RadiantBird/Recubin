#pragma once

#include <Instances/Instance.hpp>
#include <Math/CFrame.hpp>
#include <Math/Easing.hpp>
#include <string>
#include <vector>

// ==================================================================
//  Animation
//
//  時間tにおける各パーツ(Cube)のローカルCFrameをキーフレームで保持する
//  インスタンス。各トラックはパーツ名(Model相対)と昇順のキーフレーム列を持ち、
//  Humanoidがこれを再生して対象Cubeのcframeを毎フレーム補間する。
// ==================================================================

struct Keyframe {
    float      time = 0.0f;                    // 秒（0..Length）
    CFrame     cframe;                         // 対象Cubeのローカル(親=Model基準)CFrame
    EasingType easing = EasingType::Linear;    // このキー→次キーへの補間方法
};

struct AnimTrack {
    std::string           partName;            // Model相対のCube名（getChildで解決）
    std::vector<Keyframe> keyframes;           // time昇順を維持
};

class Animation : public Instance {
public:
    float Length = 1.0f;   // 全長（秒）
    float Speed  = 1.0f;   // 再生速度倍率
    bool  Looped = false;  // trueでループ再生

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

    std::vector<AnimTrack>&       getTracks()       { return m_tracks; }
    const std::vector<AnimTrack>& getTracks() const { return m_tracks; }

    // 単体の .yaml ファイルへ書き出す/読み込む（シーンとは独立した再利用用）。
    // 成功時 true を返す（失敗はリターンコードで扱い、例外は投げない）。
    bool exportToFile(const std::string& path) const;
    bool importFromFile(const std::string& path);

private:
    std::vector<AnimTrack> m_tracks;

    // partNameのトラックを返す（なければ作成）
    AnimTrack& trackFor(const std::string& partName);
};
