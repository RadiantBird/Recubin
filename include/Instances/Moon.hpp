#pragma once

#include <include/Instances/CelestialBody.hpp>
#include <include/Instances/Named.hpp>

// 太陽の反対側に見える月。位置はRendererが最初のSunのAngleから決める
// （Sunが無ければ既定のSun角の反対側）。光源にはならない。
class Moon : public Named<Moon, CelestialBody> {
public:
    static constexpr const char* ClassName = "Moon";
    static constexpr float BASE_DIAMETER = 150.0f;

    Moon();

    float baseDiameter() const override { return BASE_DIAMETER; }

    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
