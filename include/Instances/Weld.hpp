#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <memory>
#include <vector>

class SceneLoader;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

class Weld : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    PhysicsConstraintHandle m_constraintHandle;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    void registerIfReady();
    void invalidateBinding();
public:
    std::string m_cube0Name;
    std::string m_cube1Name;

    Weld();
    Weld(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~Weld();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setCube0(std::shared_ptr<BaseCube> cube);
    void setCube1(std::shared_ptr<BaseCube> cube);
    std::shared_ptr<BaseCube> getCube0() const { return m_cube0.lock(); }
    std::shared_ptr<BaseCube> getCube1() const { return m_cube1.lock(); }
    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // （Cube のリパレント/リネームでパス文字列が古くなるため）。
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames();

    // 指定キューブに Weld で連鎖接続された全 BaseCube を収集する（BFS）
    // root: 溶接ツリーを走査する起点（Workspace でも StarterCharacter/System でも可）
    static std::vector<std::shared_ptr<BaseCube>>
        collectAssembly(const std::shared_ptr<BaseCube>& start, const Instance& root);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "Weld.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "Weld.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
    }
    void remapClonedInstances(const CloneRemap& map) override;
};
