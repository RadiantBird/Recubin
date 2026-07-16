#pragma once
#include <include/Instances/ValueBase.hpp>
#include <include/Instances/Named.hpp>
#include <memory>
#include <string>

class ObjectValue : public Named<ObjectValue, ValueBase> {
    std::weak_ptr<Instance> m_target;

public:
    static constexpr const char* ClassName = "ObjectValue";

    std::string m_targetPathName;

    ObjectValue();
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;

    std::shared_ptr<Instance> getTarget() const;
    void setTarget(std::shared_ptr<Instance> target);
    void resolveTarget(std::shared_ptr<Instance> target);
    void refreshRefName();
};
