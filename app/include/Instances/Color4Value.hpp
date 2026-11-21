#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>
#include <include/Util/Color4.hpp>

class Color4Value : public Named<Color4Value, ValueBase> {
public:
    static constexpr const char* ClassName = "Color4Value";

    Color4 Value = Color4(1, 1, 1, 1);

    Color4Value();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
