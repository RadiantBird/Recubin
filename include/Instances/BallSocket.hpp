#pragma once
#include <include/Instances/PhysicsConstraint.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Attachment.hpp>
#include <memory>

class SceneLoader;
class Attachment;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

enum class BallSocketAngularMode {
    Free = 0,
    Limited = 1,
    Locked = 2,
};

class BallSocket : public PhysicsConstraint {
    std::weak_ptr<Attachment> m_attachment0; // 任意。設定時はこの位置にアンカーする
    std::weak_ptr<Attachment> m_attachment1;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    void refreshAngularBinding();

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    // 名前が設定済みで未解決のAttachment参照を対応Cube配下から遅延解決する
    void resolveAdditionalReferences() override;
public:
    BallSocketAngularMode AngularXMode = BallSocketAngularMode::Free;
    BallSocketAngularMode AngularYMode = BallSocketAngularMode::Free;
    BallSocketAngularMode AngularZMode = BallSocketAngularMode::Free;
    float AngularXMin = -180.0f;
    float AngularXMax = 180.0f;
    float AngularYMin = -180.0f;
    float AngularYMax = 180.0f;
    float AngularZMin = -180.0f;
    float AngularZMax = 180.0f;

    std::string m_attachment0Name; // Cube0配下の子孫パス（空=未使用）
    std::string m_attachment1Name; // Cube1配下の子孫パス（空=未使用）

    BallSocket();
    BallSocket(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);

    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // （Cube のリパレント/リネームでパス文字列が古くなるため）。
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames() override;

    void setAngularXMode(BallSocketAngularMode mode);
    void setAngularYMode(BallSocketAngularMode mode);
    void setAngularZMode(BallSocketAngularMode mode);
    void setAngularXMin(float angle);
    void setAngularXMax(float angle);
    void setAngularYMin(float angle);
    void setAngularYMax(float angle);
    void setAngularZMin(float angle);
    void setAngularZMax(float angle);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "BallSocket.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "BallSocket.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_attachment0.lock(), "Attachment", "BallSocket.Attachment0", [self, oldName = m_attachment0Name](std::shared_ptr<Instance> v) { self->m_attachment0 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment0Name = v ? oldName : std::string{}; }});
        out.push_back({m_attachment1.lock(), "Attachment", "BallSocket.Attachment1", [self, oldName = m_attachment1Name](std::shared_ptr<Instance> v) { self->m_attachment1 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment1Name = v ? oldName : std::string{}; }});
    }
    void remapClonedInstances(const CloneRemap& map) override;
};
