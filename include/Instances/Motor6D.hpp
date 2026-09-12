#pragma once

#include <Instances/PhysicsConstraint.hpp>
#include <Math/CFrame.hpp>

class Box3DPhysicsBackend;

class Motor6D : public PhysicsConstraint {
    friend class Box3DPhysicsBackend;
public:
    CFrame C0;
    CFrame C1;
    CFrame Transform;
    float Frequency = 10.0f;
    float DampingRatio = 1.0f;

    Motor6D();

    std::shared_ptr<BaseCube> getPart0() const;
    std::shared_ptr<BaseCube> getPart1() const;
    void setPart0(std::shared_ptr<BaseCube> part);
    void setPart1(std::shared_ptr<BaseCube> part);
    void setC0(CFrame value);
    void setC1(CFrame value);
    void setTransform(CFrame value);
    void setFrequency(float value);
    void setDampingRatio(float value);

    std::string getClassName() override;
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override;
};
