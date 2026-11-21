#pragma once

#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>

class StringValue : public Named<StringValue, ValueBase> {
public:
    static constexpr const char* ClassName = "StringValue";

    std::string Value;

    StringValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
