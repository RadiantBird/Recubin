#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>

class NumberValue : public Named<NumberValue, ValueBase> {
public:
    static constexpr const char* ClassName = "NumberValue";

    double Value = 0.0;

    NumberValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
