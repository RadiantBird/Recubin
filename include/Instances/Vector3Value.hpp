#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Vector3.hpp>

class Vector3Value : public Named<Vector3Value, ValueBase> {
public:
    static constexpr const char* ClassName = "Vector3Value";

    Vector3 Value;

    Vector3Value();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
