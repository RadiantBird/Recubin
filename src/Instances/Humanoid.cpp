#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
#include <Instances/Spatial.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <Math/Quaternion.hpp>
#include <Math/CFrame.hpp>
#include <cmath>
#include <cstdlib>

// プロパティ・メタデータ表（単一の正）。ここから Luau getter/setter・YAML 読込/保存・
// clone が一括生成される。アニメーション系メソッドと TakeDamage は LuauEngine の
// private クロージャなので従来どおり LuauEngine_Dispatch.cpp 側で登録する。
static const bool s_humanoidRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Humanoid", {
        field   <&Humanoid::WalkSpeed>  ("WalkSpeed",   0, 100).clampLua(),
        field   <&Humanoid::JumpPower>  ("JumpPower",   0, 100).clampLua(),
        field   <&Humanoid::MaxHealth>  ("MaxHealth",   0, 10000).clampLua(),
        field   <&Humanoid::RespawnTime>("RespawnTime", 0, 600).clampLua(),
        fieldVia<&Humanoid::Health, &Humanoid::setHealth>("Health", 0, 100),
        sig     <&Humanoid::Died>("Died"),
    });
    return true;
}();

Humanoid::Humanoid() : Instance("Humanoid"), Died(std::make_shared<RCBNScriptSignal>()) {}

bool Humanoid::IsA(std::string className) {
    if (className == "Humanoid") return true;
    return Instance::IsA(className);
}

void Humanoid::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Humanoid", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> Humanoid::clone() const {
    auto copy = std::make_shared<Humanoid>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Humanoid");
    // m_dead / Died は複製せず新規（=生存状態・新しいシグナル）
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

void Humanoid::setHealth(float v) {
    if (v > MaxHealth) v = MaxHealth;
    bool wasAlive = (Health > 0.0f) && !m_dead;
    Health = v;
    if (Health <= 0.0f && wasAlive) {
        Health = 0.0f;
        m_dead = true;
        if (Died) Died->fire(); // connect 済みなら m_mainL 経由で Lua へ通知
    }
}

void Humanoid::takeDamage(float n) {
    setHealth(Health - n);
}

void Humanoid::enterRagdoll(Physics* physics) {
    stopAnimation();

    // -1..1 の乱数
    auto frand = []() { return (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f; };

    // Root は不可視の物理ボディ。散乱に干渉しないよう衝突を無効化してアクターを外す
    if (Root) {
        Root->CanCollide = false;
        if (physics) physics->recreateActor(std::static_pointer_cast<BaseCube>(Root));
    }

    // 各ボディパーツを動的アクター化してランダムに吹き飛ばす
    std::shared_ptr<BaseCube> parts[] = {
        std::static_pointer_cast<BaseCube>(Torso),
        std::static_pointer_cast<BaseCube>(Head),
        std::static_pointer_cast<BaseCube>(LeftArm),
        std::static_pointer_cast<BaseCube>(RightArm),
        std::static_pointer_cast<BaseCube>(LeftLeg),
        std::static_pointer_cast<BaseCube>(RightLeg),
    };
    for (auto& part : parts) {
        if (!part) continue;
        part->CanCollide = true;
        part->Anchored   = false;
        part->LockFlags  = (physx::PxRigidDynamicLockFlags)0; // 自由に回転させる
        if (!physics) continue;
        physics->recreateActor(part);
        if (!part->actor) continue;
        auto* dyn = part->actor->is<physx::PxRigidDynamic>();
        if (!dyn) continue;
        float speed = 8.0f + (static_cast<float>(std::rand()) / RAND_MAX) * 7.0f; // 8..15
        physx::PxVec3 vel(frand() * speed, 6.0f + frand() * 4.0f, frand() * speed);
        dyn->setLinearVelocity(vel);
        dyn->setAngularVelocity(physx::PxVec3(frand() * 10.0f, frand() * 10.0f, frand() * 10.0f));
    }
}

void Humanoid::resolveParts(Instance* characterModel) {
    if (!characterModel) return;
    const auto& kids = characterModel->getChildren();

    auto find = [&kids](const char* name) -> std::shared_ptr<Instance> {
        auto it = kids.find(name);
        return (it != kids.end()) ? it->second : nullptr;
    };

    Root      = std::dynamic_pointer_cast<BaseCube>(find("Root"));
    Torso     = std::dynamic_pointer_cast<BaseCube>(find("Torso"));
    Head      = std::dynamic_pointer_cast<BaseCube>(find("Head"));
    LeftArm   = std::dynamic_pointer_cast<BaseCube>(find("LeftArm"));
    RightArm  = std::dynamic_pointer_cast<BaseCube>(find("RightArm"));
    LeftLeg   = std::dynamic_pointer_cast<BaseCube>(find("LeftLeg"));
    RightLeg  = std::dynamic_pointer_cast<BaseCube>(find("RightLeg"));

    // RootはX/Z軸の回転をロックし、転倒しないようにする（Y軸回転=向き変えのみ許可）。
    // ユーザーがStarterCharacter内に独自のCubeを"Root"として置いた場合も同じ挙動にするため、
    // 物理アクター生成前のここで毎回設定する
    if (Root) {
        Root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    }
}

void Humanoid::move(const Vector3& flatForward, const Vector3& flatRight, bool isPressingMove,
                     const Vector3& targetMoveDir, bool ctrlLockEnabled, Physics* physics,
                     bool leftArmRaised, bool rightArmRaised) {
    if (!Root || !Root->actor) return;

    physx::PxRigidDynamic* dynamicActor = Root->actor->is<physx::PxRigidDynamic>();
    if (!dynamicActor) return;

    // --- 移動ベクトルの補間 ---
    currentMoveDir = currentMoveDir + (targetMoveDir - currentMoveDir) * 0.15f;

    // --- 向き(Rotation)の更新 ---
    Quaternion targetRot = Root->Rotation;
    if (ctrlLockEnabled) {
        // CtrlLock中は常にカメラの正面方向を向く（Roblox ShiftLock方式）
        targetRot = Quaternion::LookRotation(flatForward, Vector3(0, 1, 0));
    } else if (isPressingMove) {
        targetRot = Quaternion::LookRotation(targetMoveDir, Vector3(0, 1, 0));
    }
    Root->Rotation = Quaternion::Slerp(Root->Rotation, targetRot, 0.15f);

    physx::PxTransform pose = dynamicActor->getGlobalPose();
    pose.q = physx::PxQuat(Root->Rotation.x, Root->Rotation.y, Root->Rotation.z, Root->Rotation.w);
    dynamicActor->setGlobalPose(pose);

    // --- 物理速度の適用 ---
    if (currentMoveDir.length() > 0.01f) {
        Vector3 velocity = currentMoveDir * WalkSpeed;

        RaycastHit wallHit;
        float checkDist = Root->Size.x / 2.0f + 0.15f;
        if (physics && physics->raycast(Root->getWorldPosition(), currentMoveDir, checkDist, wallHit, Root->actor)) {
            Vector3 n(wallHit.normal.x, 0.0f, wallHit.normal.z);
            float nLen = n.length();
            if (nLen > 0.001f) {
                n = n * (1.0f / nLen);
                float dot = velocity.x * n.x + velocity.z * n.z;
                if (dot < 0.0f) {
                    velocity.x -= dot * n.x;
                    velocity.z -= dot * n.z;
                }
            }
        }

        physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
        dynamicActor->setLinearVelocity(physx::PxVec3(velocity.x, currentVel.y, velocity.z));
    } else {
        physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
        dynamicActor->setLinearVelocity(physx::PxVec3(0, currentVel.y, 0));
    }

    // --- アニメーションサイクル（0.0 ~ 1.0）の更新 ---
    const float animationSpeed = 0.025f;
    if (isPressingMove) {
        walkCycle += animationSpeed;
        if (walkCycle > 1.0f) walkCycle -= 1.0f;
    } else if (walkCycle > 0.0f) {
        if (walkCycle > 0.5f) {
            walkCycle += animationSpeed;
            if (walkCycle >= 1.0f) walkCycle = 0.0f;
        } else {
            walkCycle -= animationSpeed;
            if (walkCycle < 0.0f) walkCycle = 0.0f;
        }
    }

    // --- 地面判定 ---
    RaycastHit hit;
    float maxDist = (Root->Size.y / 2.0f) + 0.2f;
    isGrounded = physics ? physics->raycast(Root->getWorldPosition(), Vector3(0, -1, 0), maxDist, hit, Root->actor) : false;

    applyBodyAnimation(leftArmRaised, rightArmRaised);
}

bool Humanoid::moveToward(const Vector3& target, Physics* physics, float arrivalRadius) {
    if (!Root) resolveParts(Parent.lock().get());
    if (!Root) return false;

    Vector3 toTarget = target - Root->getWorldPosition();
    toTarget.y = 0.0f;
    float dist = toTarget.length();
    if (dist <= arrivalRadius) {
        move(Vector3(0, 0, -1), Vector3(1, 0, 0), false, Vector3(0, 0, 0), false, physics);
        return true;
    }

    Vector3 dir = toTarget.normalize();
    move(dir, Vector3::Cross(Vector3(0, 1, 0), dir), true, dir, false, physics);
    return false;
}

void Humanoid::jump() {
    if (!Root) resolveParts(Parent.lock().get());
    if (!Root || !Root->actor || !isGrounded) return;
    physx::PxRigidDynamic* dynamicActor = Root->actor->is<physx::PxRigidDynamic>();
    if (!dynamicActor) return;
    physx::PxVec3 vel = dynamicActor->getLinearVelocity();
    vel.y = JumpPower;
    dynamicActor->setLinearVelocity(vel);
}

// ============================================================
// Animation: 再生
// ============================================================

void Humanoid::playAnimation(std::shared_ptr<Animation> animation) {
    m_currentAnim = std::move(animation);
    m_animTime = 0.0f;
    m_animPlaying = (m_currentAnim != nullptr);
}

void Humanoid::pauseAnimation() {
    m_animPlaying = false;
}

void Humanoid::stopAnimation() {
    m_animPlaying = false;
    m_animTime = 0.0f;
}

void Humanoid::setAnimationSpeed(float speed) {
    if (m_currentAnim) m_currentAnim->Speed = speed;
}

void Humanoid::updateAnimation(float dt) {
    if (!m_animPlaying || !m_currentAnim) return;

    // 対象パーツの解決先となるModel(=このHumanoidの親)
    Instance* model = Parent.lock().get();
    if (!model) return;

    m_animTime += dt * m_currentAnim->Speed;
    if (m_currentAnim->Length > 1e-6f) {
        while (m_animTime > m_currentAnim->Length)
            m_animTime -= m_currentAnim->Length; // ループ再生
    }

    // キーフレームはRoot相対で保持されているため、現在のRoot CFrameに合成して
    // キャラクターの移動・回転に追従させる（歩行アニメと同じ基準）
    CFrame rootCF = Root ? Root->cframe : CFrame();
    for (const AnimTrack& track : m_currentAnim->getTracks()) {
        Instance* child = model->getChild(track.partName);
        Spatial* part = dynamic_cast<Spatial*>(child);
        if (!part || part == Root.get()) continue; // Rootは物理駆動なので動かさない
        part->cframe = rootCF * m_currentAnim->evaluateTrack(track, m_animTime);
    }
}

void Humanoid::updateFirstPersonState(bool wantsFirstPerson) {
    if (!Root || !Torso || !Head || !LeftArm || !RightArm || !LeftLeg || !RightLeg) return;

    if (wantsFirstPerson && !isFirstPerson) {
        if (!bodyColorsSaved) {
            savedTorsoColor    = Torso->Color;
            savedHeadColor     = Head->Color;
            savedLeftArmColor  = LeftArm->Color;
            savedRightArmColor = RightArm->Color;
            savedLeftLegColor  = LeftLeg->Color;
            savedRightLegColor = RightLeg->Color;
            bodyColorsSaved = true;
        }
        Color4 hidden = Color4(1.0f, 1.0f, 1.0f, 0.0f);
        Torso->Color    = hidden;
        Head->Color     = hidden;
        LeftArm->Color  = hidden;
        RightArm->Color = hidden;
        LeftLeg->Color  = hidden;
        RightLeg->Color = hidden;
        isFirstPerson = true;
    } else if (!wantsFirstPerson && isFirstPerson) {
        if (bodyColorsSaved) {
            Torso->Color    = savedTorsoColor;
            Head->Color     = savedHeadColor;
            LeftArm->Color  = savedLeftArmColor;
            RightArm->Color = savedRightArmColor;
            LeftLeg->Color  = savedLeftLegColor;
            RightLeg->Color = savedRightLegColor;
            bodyColorsSaved = false;
        }
        isFirstPerson = false;
    }
}

Vector3 Humanoid::getRootWorldPosition() const {
    return Root ? Root->getWorldPosition() : Vector3(0, 0, 0);
}

Vector3 Humanoid::getHeadWorldPosition() const {
    return Head ? Head->getWorldPosition() : getRootWorldPosition();
}

// ============================================================
// Animation: Pose計算
// ============================================================

Humanoid::Pose Humanoid::computePose(bool leftArmRaised, bool rightArmRaised) const {
    const float PI = 3.14159265f;
    float rad   = walkCycle * 2.0f * PI;
    float swing = std::sin(rad) * 35.0f;

    Pose p;
    p.leftArm  = leftArmRaised  ? 90.0f : (isGrounded ? swing  : 180.0f);
    p.rightArm = rightArmRaised ? 90.0f : (isGrounded ? -swing : 180.0f);
    p.leftLeg  = -swing;
    p.rightLeg =  swing;
    return p;
}

// ============================================================
// Animation: Limb組み立て（共通）
// ============================================================

static CFrame makeArm(
    const CFrame& root,
    const Vector3& jointPos,
    float angleDeg
) {
    const Vector3 pivotOffset = Vector3(0, -0.5f, 0); // 回転中心調整
    const Vector3 meshOffset  = Vector3(0, -1.0f, 0); // モデル補正

    return root *
           CFrame(jointPos.x, jointPos.y, jointPos.z) *
           CFrame(pivotOffset.x, pivotOffset.y, pivotOffset.z) *
           CFrame::fromAxisAngle(Vector3(1,0,0), angleDeg) *
           CFrame(-pivotOffset.x, -pivotOffset.y, -pivotOffset.z) *
           CFrame(meshOffset.x, meshOffset.y, meshOffset.z);
}

static CFrame makeLeg(
    const CFrame& root,
    const Vector3& jointPos,
    float angleDeg
) {
    // 脚は今のところpivot補正なし
    const Vector3 meshOffset = Vector3(0, -1.0f, 0);

    return root *
           CFrame(jointPos.x, jointPos.y, jointPos.z) *
           CFrame::fromAxisAngle(Vector3(1,0,0), angleDeg) *
           CFrame(meshOffset.x, meshOffset.y, meshOffset.z);
}

void Humanoid::applyBodyAnimation(bool leftArmRaised, bool rightArmRaised) {
    if (!Root) return;

    Pose pose = computePose(leftArmRaised, rightArmRaised);

    // --- リグ定義（全部ここに固定） ---
    const Vector3 torsoOffset      = Vector3(0, 1.0f, 0);
    const Vector3 headOffset       = Vector3(0, 2.5f, 0);

    const Vector3 leftShoulderPos  = Vector3(-1.5f, 2.0f, 0);
    const Vector3 rightShoulderPos = Vector3( 1.5f, 2.0f, 0);

    const Vector3 leftHipPos       = Vector3(-0.5f, 0.0f, 0);
    const Vector3 rightHipPos      = Vector3( 0.5f, 0.0f, 0);

    if (Torso) Torso->cframe = Root->cframe * CFrame(torsoOffset.x, torsoOffset.y, torsoOffset.z);
    if (Head)  Head->cframe  = Root->cframe * CFrame(headOffset.x,  headOffset.y,  headOffset.z);

    if (LeftArm)  LeftArm->cframe  = makeArm(Root->cframe, leftShoulderPos,  pose.leftArm);
    if (RightArm) RightArm->cframe = makeArm(Root->cframe, rightShoulderPos, pose.rightArm);
    if (LeftLeg)  LeftLeg->cframe  = makeLeg(Root->cframe, leftHipPos,  pose.leftLeg);
    if (RightLeg) RightLeg->cframe = makeLeg(Root->cframe, rightHipPos, pose.rightLeg);
}
