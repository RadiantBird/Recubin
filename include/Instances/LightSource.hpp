#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Util/Color4.hpp>
#include <string>

// Point/Spot ライトの共通基底。位置は親 Spatial のワールド CFrame から得る（Roblox風）。
class LightSource : public Instance {
public:
    Color4 lightColor = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    float  brightness = 1.0f;
    float  range      = 16.0f;   // 減衰半径（studs）

    explicit LightSource(std::string className);
    virtual ~LightSource() = default;

    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
};
