#pragma once

#include <include/Instances/CelestialBody.hpp>
#include <include/Instances/Named.hpp>

// 太陽の反対側に見える月。位置はRendererが最初のSunのAngleから決める
// （Sunが無ければ既定のSun角の反対側）。光源にはならない。
//
// 満ち欠け（Phase）: 満月（既定）以外のときは、通常の発光球ではなく、明暗境界を持つ円盤を
// sky_flare シェーダーで描く（影の部分は Earthshine だけ透けて空が見える）。
class Moon : public Named<Moon, CelestialBody> {
public:
    static constexpr const char* ClassName = "Moon";
    static constexpr float BASE_DIAMETER = 150.0f;
    static constexpr float FULL_PHASE = 0.5f;

    // 月の満ち欠けの周期上の位置。0 = 新月、0.25 = 上弦（右が明るい）、0.5 = 満月（既定）、
    // 0.75 = 下弦（左が明るい）、1 = 新月。範囲外の値は周期として折り返す。
    float Phase = FULL_PHASE;
    // 明暗境界の向き（度）。0で、上弦は右・下弦は左が明るい（画面基準）。
    float PhaseRotation = 0.0f;
    // 影の部分がどれだけ見えるか（0 = 透明で空が見える、1 = 不透明）。地球照のような薄い暗部。
    float Earthshine = 0.08f;

    Moon();

    float baseDiameter() const override { return BASE_DIAMETER; }

    // 周期へ折り返した満ち欠けの位置 [0,1)。非有限値は満月として扱う。
    float wrappedPhase() const;
    // 満月以外か（満ち欠けの円盤で描くべきか）。
    bool hasPhase() const;

    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
