#pragma once

#include <include/Instances/Sphere.hpp>
#include <include/Instances/Named.hpp>

class Sun : public Named<Sun, Sphere> {
public:
    static constexpr const char* ClassName = "Sun";

    float Angle = 45.0f;  // 公転角（度）: 0=+Z水平 90=天頂 180=-Z水平

    Sun();

    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
