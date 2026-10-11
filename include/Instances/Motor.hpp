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

class Motor : public PhysicsConstraint {
    std::weak_ptr<Attachment> m_attachment0; // 任意。設定時はこの位置でジョイントを生成
    std::weak_ptr<Attachment> m_attachment1;

    friend class Physics;
    friend class PhysXPhysicsBackend;
    friend class Box3DPhysicsBackend;
    friend class SceneLoader;
    friend class Weld;
    friend class Renderer;

    // 両方のCubeが解決済みなら制約をWorkspaceに登録する（setProperty/setCube0/setCube1から共通利用）
    // 名前が設定済みで未解決のAttachment参照を対応Cube配下から遅延解決する
    void resolveAdditionalReferences() override;
    // Axisや参照先などjoint frameが変わるプロパティの変更時だけnative jointを再生成する
    void recreateConstraint();

    // 最初にnative jointを作ったときの、各Cubeから見たジョイントの支点と、Cube1から見たジョイントの向き。
    // 着席(Weld合成)やAnchoredの切り替えでボディが作り直されるとjointも作り直されるが、そのとき
    // その瞬間の(旋回の負荷などで傾いた)姿勢を新しい基準にすると、傾きが積み上がって車軸が曲がる。
    // 作り直しではこの基準を使う。参照先・Axis・Attachmentが変わったら使わない。保存・複製はしない
    struct NativeJointFrames {
        const BaseCube* cube0 = nullptr;
        const BaseCube* cube1 = nullptr;
        const Attachment* attachment0 = nullptr;
        const Attachment* attachment1 = nullptr;
        Vector3 axis{};
        Vector3 pivot0{};     // Cube0ローカル
        Vector3 pivot1{};     // Cube1ローカル
        Quaternion rotation1; // Cube1ローカルのジョイントの向き
        bool valid = false;
    };
    NativeJointFrames m_nativeJointFrames;
public:
    Vector3 Axis          = {1.0f, 0.0f, 0.0f}; // 回転軸（Cube0基準のローカル方向）
    float DriveVelocity   = 1.0f;  // rad/s
    float MaxForce        = 1000.0f;
    // true: DriveVelocity による連続回転ではなく、TargetAngleを維持するサーボとして動く。
    // このときDriveVelocityは最大回転速度(|rad/s|)、MaxForceは出せる最大トルクとして働く。
    bool  Servo           = false;
    // Servo有効時の目標角度(度)。Axis周りの右ねじ方向が正で、最初のジョイント生成時の姿勢が0度。
    // 物理側の角度は[-180,180]で扱うため、目標との差は最短経路で解く。
    float TargetAngle     = 0.0f;

    std::string m_attachment0Name; // Cube0配下の子孫パス（空=未使用）
    std::string m_attachment1Name; // Cube1配下の子孫パス（空=未使用）

    Motor();
    Motor(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1);

    // セーブ直前に呼ばれ、生きている参照から現在の正しいパスを再生成する
    // （Cube のリパレント/リネームでパス文字列が古くなるため）。
    // 名前が空 = 「未設定」の正当な状態なので復活させない
    void refreshRefNames() override;
    void setDriveVelocity(float v);
    void setMaxForce(float v);
    void setAxis(Vector3 axis);
    void setServo(bool enabled);
    void setTargetAngle(float degrees);
    float getDriveVelocity() const { return DriveVelocity; }
    float getMaxForce() const { return MaxForce; }
    bool getServo() const { return Servo; }
    float getTargetAngle() const { return TargetAngle; }
    PhysicsConstraintHandle getConstraintHandle() const;

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    void remapClonedInstances(const CloneRemap& map) override;
    void collectInstanceReferences(std::vector<InstanceReference>& out) override {
        auto self = this;
        out.push_back({m_cube0.lock(), "BaseCube", "Motor.Cube0", [self](std::shared_ptr<Instance> v) { self->setCube0(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_cube1.lock(), "BaseCube", "Motor.Cube1", [self](std::shared_ptr<Instance> v) { self->setCube1(std::dynamic_pointer_cast<BaseCube>(v)); }});
        out.push_back({m_attachment0.lock(), "Attachment", "Motor.Attachment0", [self, oldName = m_attachment0Name](std::shared_ptr<Instance> v) { self->m_attachment0 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment0Name = v ? oldName : std::string{}; }});
        out.push_back({m_attachment1.lock(), "Attachment", "Motor.Attachment1", [self, oldName = m_attachment1Name](std::shared_ptr<Instance> v) { self->m_attachment1 = std::dynamic_pointer_cast<Attachment>(v); self->m_attachment1Name = v ? oldName : std::string{}; }});
    }
};
