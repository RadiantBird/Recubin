#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Quaternion.hpp>

class QuaternionValue : public Named<QuaternionValue, ValueBase> {
public:
    static constexpr const char* ClassName = "QuaternionValue";

    Quaternion Value;

    QuaternionValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
