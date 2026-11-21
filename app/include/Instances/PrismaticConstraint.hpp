#pragma once
#include <include/Instances/PhysicsConstraint.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Math/Vector3.hpp>
#include <memory>

class SceneLoader;
class Attachment;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

// Axis方向にのみ相対移動を許す直動拘束。Box3D b3PrismaticJoint。
// Attachment無しなら各Cubeの中心がアンカー。
class PrismaticConstraint : public PhysicsConstraint {
    std::weak_ptr<Attachment> m_attachment0; // 任意。設定時はこの位置/軸系でjointを生成
    std::weak_ptr<Attachment> m_attachment1;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    // 名前が設定済みで未解決のAttachment参照を対応Cube配下から遅延解決する
    void resolveAdditionalReferences() override;
    // Axisなどjoint frameが変わるプロパティの変更時にnative jointを再生成する
    void recreateConstraint();
    // 構築済みの物理ジョイントがあれば制限だけ更新する
    void notifyPhysicsUpdate();
public:
    Vector3 Axis = {0.0f, 1.0f, 0.0f}; // 移動軸（Cube0ローカル。Attachment0があればその軸系）
    bool  LimitsEnabled = false;
    float LowerLimit    = -5.0f; // stud
    float UpperLimit    = 5.0f;  // stud

    std::string m_attachment0Name; // Cube0配下の子孫パス（空=未使用）
    std::string m_attachment1Name; // Cube1配下の子孫パス（空=未使用）

    PrismaticConstraint();
    PrismaticConstraint(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);

    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames() override;
    void setAxis(Vector3 axis);
    void setLimitsEnabled(bool enabled);
    void setLowerLimit(float v);
    void setUpperLimit(float v);
    bool getLimitsEnabled() const { return LimitsEnabled; }
    float getLowerLimit() const { return LowerLimit; }
    float getUpperLimit() const { return UpperLimit; }

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "PrismaticConstraint.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "PrismaticConstraint.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_attachment0.lock(), "Attachment", "PrismaticConstraint.Attachment0", [self, oldName = m_attachment0Name](std::shared_ptr<Instance> v) { self->m_attachment0 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment0Name = v ? oldName : std::string{}; }});
        out.push_back({m_attachment1.lock(), "Attachment", "PrismaticConstraint.Attachment1", [self, oldName = m_attachment1Name](std::shared_ptr<Instance> v) { self->m_attachment1 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment1Name = v ? oldName : std::string{}; }});
    }
};
