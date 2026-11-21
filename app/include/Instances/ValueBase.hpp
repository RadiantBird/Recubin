#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Core/RCBNScriptSignal.hpp>
#include <memory>
#include <string>

class ValueBase : public Instance {
public:
    std::shared_ptr<RCBNScriptSignal> Changed;

    explicit ValueBase(std::string className);
    virtual ~ValueBase() = default;

    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
};
