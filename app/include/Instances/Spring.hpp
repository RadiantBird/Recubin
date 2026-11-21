#pragma once
#include <include/Instances/PhysicsConstraint.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Util/Color4.hpp>
#include <memory>

class SceneLoader;
class Attachment;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

// 自然長(FreeLength)へ引きも押しも行う双方向ばね。Box3D b3DistanceJoint + enableSpring。
// Attachment無しなら各Cubeの中心がアンカー。
class Spring : public PhysicsConstraint {
    std::weak_ptr<Attachment> m_attachment0; // 任意。設定時はこの位置にアンカーする
    std::weak_ptr<Attachment> m_attachment1;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    // 名前が設定済みで未解決のAttachment参照を対応Cube配下から遅延解決する
    void resolveAdditionalReferences() override;
    // 構築済みの物理ジョイントがあればパラメータだけ更新する
    void notifyPhysicsUpdate();
public:
    float FreeLength = 0.0f; // 0 = 生成時の距離を自然長に採用
    float Stiffness  = 100.0f;
    float Damping    = 10.0f;

    std::string m_attachment0Name; // Cube0配下の子孫パス（空=未使用）
    std::string m_attachment1Name; // Cube1配下の子孫パス（空=未使用）

    bool   Visible   = true;
    Color4 Color     = {0.9f, 0.8f, 0.3f, 1.0f};
    float  Radius    = 0.5f;  // コイル半径(stud)
    int    Coils     = 8;     // 巻き数
    float  Thickness = 0.1f;  // 線の太さ(stud)

    Spring();
    Spring(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);

    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames() override;
    void setFreeLength(float v);
    void setStiffness(float v);
    void setDamping(float v);
    float getFreeLength() const { return FreeLength; }
    float getStiffness() const { return Stiffness; }
    float getDamping() const { return Damping; }

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "Spring.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "Spring.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_attachment0.lock(), "Attachment", "Spring.Attachment0", [self, oldName = m_attachment0Name](std::shared_ptr<Instance> v) { self->m_attachment0 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment0Name = v ? oldName : std::string{}; }});
        out.push_back({m_attachment1.lock(), "Attachment", "Spring.Attachment1", [self, oldName = m_attachment1Name](std::shared_ptr<Instance> v) { self->m_attachment1 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment1Name = v ? oldName : std::string{}; }});
    }
};
