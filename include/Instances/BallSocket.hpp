#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Attachment.hpp>
#include <memory>

class SceneLoader;
class Attachment;
class PhysXPhysicsBackend;
class Box3DPhysicsBackend;

class BallSocket : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    std::weak_ptr<Attachment> m_attachment0; // 任意。設定時はこの位置にアンカーする
    std::weak_ptr<Attachment> m_attachment1;
    PhysicsConstraintHandle m_constraintHandle;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    void registerIfReady();
    // 名前が設定済みで未解決のAttachment参照を対応Cube配下から遅延解決する
    void resolveAttachments();
public:
    std::string m_cube0Name;
    std::string m_cube1Name;
    std::string m_attachment0Name; // Cube0配下の子孫パス（空=未使用）
    std::string m_attachment1Name; // Cube1配下の子孫パス（空=未使用）

    BallSocket();
    BallSocket(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~BallSocket();

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
        out.push_back({m_cube0.lock(), "BaseCube", "BallSocket.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "BallSocket.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_attachment0.lock(), "Attachment", "BallSocket.Attachment0", [self, oldName = m_attachment0Name](std::shared_ptr<Instance> v) { self->m_attachment0 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment0Name = v ? oldName : std::string{}; }});
        out.push_back({m_attachment1.lock(), "Attachment", "BallSocket.Attachment1", [self, oldName = m_attachment1Name](std::shared_ptr<Instance> v) { self->m_attachment1 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment1Name = v ? oldName : std::string{}; }});
    }
    void remapClonedInstances(const CloneRemap& map) override;
};
