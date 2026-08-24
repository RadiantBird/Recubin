#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <memory>

class SceneLoader;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

class NoCollision : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    Workspace* m_lastWorkspace = nullptr;
    PhysicsConstraintHandle m_constraintHandle;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    void registerIfReady();
public:
    std::string m_cube0Name;
    std::string m_cube1Name;

    NoCollision();
    NoCollision(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~NoCollision();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setCube0(std::shared_ptr<BaseCube> cube);
    void setCube1(std::shared_ptr<BaseCube> cube);
    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // （Cube のリパレント/リネームでパス文字列が古くなるため）。
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames();

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "NoCollision.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "NoCollision.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
    }
    void remapClonedInstances(const CloneRemap& map) override;
};
