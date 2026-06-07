#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Util/Color4.hpp>
#include <memory>

class SceneLoader;

class Rope : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    physx::PxDistanceJoint* m_joint = nullptr;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class SceneLoader;
    friend class Renderer;
public:
    float MaxDistance = 0.0f; // 0 = 生成時の距離を自動使用
    float Stiffness   = 100.0f;
    float Damping     = 10.0f;

    std::string m_cube0Name;
    std::string m_cube1Name;
    Color4 Color     = {0.3f, 0.9f, 1.0f, 1.0f};
    float  LineWidth = 2.5f;

    Rope();
    Rope(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~Rope();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setMaxDistance(float v);
    void setStiffness(float v);
    void setDamping(float v);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
};
