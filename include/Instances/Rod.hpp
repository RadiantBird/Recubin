#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Util/Color4.hpp>
#include <memory>

class SceneLoader;

class Rod : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    physx::PxDistanceJoint* m_joint = nullptr;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class SceneLoader;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    void registerIfReady();
public:
    std::string m_cube0Name;
    std::string m_cube1Name;
    Color4 Color     = {1.0f, 0.6f, 0.1f, 1.0f};
    float  LineWidth = 2.5f;

    Rod();
    Rod(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~Rod();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setCube0(std::shared_ptr<BaseCube> cube);
    void setCube1(std::shared_ptr<BaseCube> cube);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedCubes(const CubeRemap& map) override;
};
