#pragma once
#include <include/Instances/LightSource.hpp>
#include <include/Instances/Named.hpp>

class PointLight : public Named<PointLight, LightSource> {
public:
    static constexpr const char* ClassName = "PointLight";

    PointLight();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
