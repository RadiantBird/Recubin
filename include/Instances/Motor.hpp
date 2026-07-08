#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Math/Vector3.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <memory>

class SceneLoader;
class Attachment;

class Motor : public Instance {
    std::weak_ptr<BaseCube> m_cube0;
    std::weak_ptr<BaseCube> m_cube1;
    std::weak_ptr<Attachment> m_attachment0; // 任意。設定時はこの位置でジョイントを生成
    std::weak_ptr<Attachment> m_attachment1;
    physx::PxRevoluteJoint* m_joint = nullptr;
    Workspace* m_lastWorkspace = nullptr;

    friend class Physics;
    friend class SceneLoader;
    friend class Weld;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    void registerIfReady();
    // 名前が設定済みで未解決のAttachment参照を対応Cube配下から遅延解決する
    void resolveAttachments();
public:
    Vector3 Axis          = {1.0f, 0.0f, 0.0f}; // 回転軸（ワールド方向）
    float DriveVelocity   = 1.0f;  // rad/s
    float MaxForce        = 1000.0f;

    std::string m_cube0Name;
    std::string m_cube1Name;
    std::string m_attachment0Name; // Cube0配下の子孫パス（空=未使用）
    std::string m_attachment1Name; // Cube1配下の子孫パス（空=未使用）

    Motor();
    Motor(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    virtual ~Motor();

    void setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);
    void setCube0(std::shared_ptr<BaseCube> cube);
    void setCube1(std::shared_ptr<BaseCube> cube);
    void setDriveVelocity(float v);
    void setMaxForce(float v);

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;
};
