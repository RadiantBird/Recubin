#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <memory>
#include <vector>

class SceneLoader;

class Weld : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    physx::PxRigidDynamic*  m_compound = nullptr;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class SceneLoader;
public:
    std::string m_cube0Name;
    std::string m_cube1Name;

    Weld();
    Weld(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~Weld();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);

    // 指定キューブに Weld で連鎖接続された全 BaseCube を収集する（BFS）
    static std::vector<std::shared_ptr<BaseCube>>
        collectAssembly(const std::shared_ptr<BaseCube>& start, const Workspace& ws);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
};
