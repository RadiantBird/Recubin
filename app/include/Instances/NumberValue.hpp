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
    // Valueを代入してChangedを発火する（YAML/Luau/エディターの共通経路）
    void setValue(double value);
    std::shared_ptr<Instance> clone() const override;
};
