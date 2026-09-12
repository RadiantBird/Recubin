#pragma once

#include <Instances/PhysicsConstraint.hpp>

#include <memory>
#include <string>
#include <vector>

class BaseCube;
struct Quaternion;
struct Vector3;

enum class GyroAxis {
    X,
    Y,
    Z
};

struct GyroAxisSettings {
    bool Enabled = false;

    // @RadiantBird 2026/09/12:
    // Gyro angles use Recubin's normal Euler angle unit (degrees).
    // Do not replace the per-axis target with one TargetRotation quaternion.
    float TargetAngle = 0.0f;

    // Recubin torque unit. Box3D converts this to N*m at the backend boundary.
    float MaxTorque = 10000.0f;

    // Degrees per second.
    float MaxAngularSpeed = 180.0f;
};

class Gyro : public PhysicsConstraint {
protected:
    // @RadiantBird 2026/09/12:
    // Gyro is a single-body world angular controller.
    // It intentionally requires only Part, unlike ordinary two-endpoint constraints.
    bool endpointsReady() const override;

private:
    GyroAxisSettings m_xAxis;
    GyroAxisSettings m_yAxis;
    GyroAxisSettings m_zAxis;

    GyroAxisSettings& axisSettings(GyroAxis axis);
    const GyroAxisSettings& axisSettings(GyroAxis axis) const;

    void notifyChanged();

public:
    Gyro();

    std::shared_ptr<BaseCube> getPart() const;
    void setPart(std::shared_ptr<BaseCube> part);

    const GyroAxisSettings& getAxisSettings(GyroAxis axis) const;

    static float headingAngleFromDirection(const Vector3& direction);
    static float angleFromRotation(
        GyroAxis axis,
        const Quaternion& rotation);
    void setCharacterHeading(const Vector3& direction);
    void setCharacterRotation(const Quaternion& rotation);

    void setAxisEnabled(
        GyroAxis axis,
        bool enabled
    );

    void setTargetAngle(
        GyroAxis axis,
        float angle
    );

    void setMaxTorque(
        GyroAxis axis,
        float torque
    );

    void setMaxAngularSpeed(
        GyroAxis axis,
        float speed
    );

    std::string getClassName() override;
    bool IsA(std::string className) override;

    void setProperty(
        const std::string& name,
        const YAML::Node& value
    ) override;

    std::shared_ptr<Instance> clone() const override;

    void remapClonedInstances(
        const CloneRemap& map
    ) override;

    void collectInstanceReferences(
        std::vector<InstanceReference>& out
    ) override;
};
