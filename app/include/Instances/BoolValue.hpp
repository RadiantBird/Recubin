#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>

class BoolValue : public Named<BoolValue, ValueBase> {
public:
    static constexpr const char* ClassName = "BoolValue";

    bool Value = false;

    BoolValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
