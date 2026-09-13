#pragma once

#include <include/Instances/Instance.hpp>
#include <include/Core/PhysicsTypes.hpp>
#include <memory>

class BaseCube;
class Workspace;
class Physics;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;
class SceneLoader;
class Renderer;

// 物理制約に共通する有効状態、Cube参照、Workspace登録状態を保持する基底クラス。
class PhysicsConstraint : public Instance {
    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;
    friend class Workspace;
protected:
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    PhysicsConstraintHandle m_constraintHandle;
    Workspace* m_lastWorkspace = nullptr;

    void registerIfReady();
    // Most constraints connect two physical endpoints. Single-body controllers
    // override this without fabricating a second endpoint.
    virtual bool endpointsReady() const;
    virtual void resolveAdditionalReferences() {}
    virtual bool additionalReferencesReady() const { return true; }
    // endpoint/property変更時に既存 native binding を安全窓で破棄し、次回の
    // registerIfReady() による pending 登録へ戻す共通 dirty 経路。
    void invalidateBinding();

public:
    bool Enabled = true;
    std::string m_cube0Name;
    std::string m_cube1Name;

    explicit PhysicsConstraint(const std::string& className);
    ~PhysicsConstraint() override;

    bool IsA(std::string className) override;

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setCube0(std::shared_ptr<BaseCube> cube);
    void setCube1(std::shared_ptr<BaseCube> cube);
    std::shared_ptr<BaseCube> getCube0() const;
    std::shared_ptr<BaseCube> getCube1() const;
    PhysicsConstraintHandle getConstraintHandle() const;
    virtual void refreshRefNames();
    void setEnabled(bool enabled);
    // SceneLoader/editor が参照解決完了後に一度だけ呼ぶ公開入口。
    void resolveReferencesAndRegister();
    void onAncestorChanged() override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
};
