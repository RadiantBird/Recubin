#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Math/Vector3.hpp>
#include <string>

class Lighting : public Instance {
public:
    Vector3      lightDir   = Vector3(1.0f, -1.0f, -1.0f);
    float        brightness = 1.0f;

    Lighting();
    virtual ~Lighting() = default;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};
