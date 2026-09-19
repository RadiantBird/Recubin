#pragma once
#include <include/Instances/PhysicsConstraint.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Math/CFrame.hpp>
#include <memory>
#include <vector>

class SceneLoader;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

class Weld : public PhysicsConstraint {

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    void invalidateBinding();
public:

    Weld();
    Weld(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);

    // Runtime-only frame pair used by ToolGrip. It is deliberately not part
    // of the property registry or clone state.
    void setFrameOverride(const CFrame& frame0, const CFrame& frame1);
    void clearFrameOverride();
    bool hasFrameOverride() const { return m_hasFrameOverride; }
    const CFrame& getFrame0Override() const { return m_frame0Override; }
    const CFrame& getFrame1Override() const { return m_frame1Override; }

    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // （Cube のリパレント/リネームでパス文字列が古くなるため）。
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames() override;

    // 指定キューブに Weld で連鎖接続された全 BaseCube を収集する（BFS）
    // root: 溶接ツリーを走査する起点（Workspace でも StarterCharacter/System でも可）
    static std::vector<std::shared_ptr<BaseCube>>
        collectAssembly(const std::shared_ptr<BaseCube>& start, const Instance& root);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "Weld.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "Weld.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
    }
    void remapClonedInstances(const CloneRemap& map) override;

private:
    CFrame m_frame0Override;
    CFrame m_frame1Override;
    bool m_hasFrameOverride = false;
};
