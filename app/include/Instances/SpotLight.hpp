#pragma once
#include <include/Instances/LightSource.hpp>
#include <include/Instances/Named.hpp>

class SpotLight : public Named<SpotLight, LightSource> {
public:
    static constexpr const char* ClassName = "SpotLight";

    float Angle = 45.0f;   // コーン半角（度）

    SpotLight();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
