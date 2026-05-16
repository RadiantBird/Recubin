#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Math/Vector3.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <memory>

class SceneLoader;

class Motor : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    physx::PxRevoluteJoint* m_joint = nullptr;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class SceneLoader;
    friend class Weld;
public:
    Vector3 Axis          = {1.0f, 0.0f, 0.0f}; // 回転軸（ワールド方向）
    float DriveVelocity   = 1.0f;  // rad/s
    float MaxForce        = 1000.0f;

    std::string m_cube0Name;
    std::string m_cube1Name;

    Motor();
    Motor(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~Motor();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setDriveVelocity(float v);
    void setMaxForce(float v);

    virtual std::string GetClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
};
