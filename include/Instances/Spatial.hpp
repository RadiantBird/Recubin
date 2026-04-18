#pragma once

#include "Instance.hpp"
#include "Math/Vector3.hpp"
#include "Math/Quaternion.hpp"
#include "Math/CFrame.hpp"

class Spatial : public Instance {
public:
    CFrame cframe;
    Vector3 Size;

    // 冗長さを省くためのエイリアス参照
    Vector3& Position;
    Quaternion& Rotation;

    Spatial(Vector3 Pos, Vector3 Sz, std::string name) 
        : Instance(name), cframe(Pos), Size(Sz), 
          Position(cframe.Position), Rotation(cframe.Rotation) {}
    std::string GetClassName() override { return "Spatial"; }
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
};