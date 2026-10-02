#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Math/Vector3.hpp>
#include <include/Util/Color4.hpp>
#include <optional>
#include <string>

// シーン全体の平行光源（太陽光）の強さ・色と影の設定。光の向きは持たず、Workspaceの
// 最初のSun（Sun::Angle）が決める。Sunが無いと平行光源は出ない。
class Lighting : public Instance {
public:
    // 旧形式のYAMLが持っていた Direction（光の進行方向）。Directionは廃止されたため、
    // 読み込み時にここへ保持するだけで、保存・クローン・Lua公開はしない。Sunを持たない旧シーンの
    // 向きを、SceneRuntimeが自動生成するSunへ引き継ぐために使う。
    std::optional<Vector3> legacyDirection;
    float        brightness = 1.0f;
    Color4       lightColor = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    float        shadowDistance = 160.0f;
    float        shadowFadeDistance = 20.0f;

    Lighting();
    virtual ~Lighting() = default;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};
