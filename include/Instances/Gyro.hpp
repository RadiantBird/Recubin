#pragma once

#include <Instances/PhysicsConstraint.hpp>
#include <Math/Quaternion.hpp>

class Box3DPhysicsBackend;

class Gyro : public PhysicsConstraint {
    friend class Box3DPhysicsBackend;
public:
    Quaternion TargetRotation;
    float Frequency = 8.0f;
    float DampingRatio = 1.0f;
    float MaxTorque = 10000.0f;

    Gyro();

    std::shared_ptr<BaseCube> getPart() const;
    void setPart(std::shared_ptr<BaseCube> part);
    void setTargetRotation(Quaternion value);
    void setFrequency(float value);
    void setDampingRatio(float value);
    void setMaxTorque(float value);

    std::string getClassName() override;
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override;
};
