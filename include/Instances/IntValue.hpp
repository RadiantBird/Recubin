#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>

class IntValue : public Named<IntValue, ValueBase> {
public:
    static constexpr const char* ClassName = "IntValue";

    int Value = 0;

    IntValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
