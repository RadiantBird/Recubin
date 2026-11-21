#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/CFrame.hpp>

class CFrameValue : public Named<CFrameValue, ValueBase> {
public:
    static constexpr const char* ClassName = "CFrameValue";

    CFrame Value;

    CFrameValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
