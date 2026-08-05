#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <Math/Quaternion.hpp>
#include <Math/CFrame.hpp>
#include <Math/Units.hpp>
#include <algorithm>
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
        field   <&Humanoid::ClimbSpeed> ("ClimbSpeed",  0, 100).clampLua(),
        method_prop<&Humanoid::getJumpHeight, &Humanoid::setJumpHeight>("JumpHeight", 0, 50, 0.1f),
        field   <&Humanoid::MaxHealth>  ("MaxHealth",   0, 10000).clampLua(),
        field   <&Humanoid::RespawnTime>("RespawnTime", 0, 600).clampLua(),
        fieldVia<&Humanoid::Health, &Humanoid::setHealth>("Health", 0, 100),
        sig     <&Humanoid::Died>("Died"),
    });
    return true;
}();

Humanoid::Humanoid() : Instance("Humanoid"), Died(std::make_shared<RCBNScriptSignal>()),
    KeyframeReached(std::make_shared<RCBNScriptSignal>()) {}

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
    // m_dead / Died / KeyframeReached は複製せず新規（=生存状態・新しいシグナル）
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

std::shared_ptr<BaseCube> Humanoid::getRootPart() const {
    return m_root.lock();
}

std::shared_ptr<BaseCube> Humanoid::getTorsoPart() const {
    return m_torso.lock();
}

std::shared_ptr<BaseCube> Humanoid::getHeadPart() const {
    return m_head.lock();
}

std::shared_ptr<BaseCube> Humanoid::getLeftArmPart() const {
    return m_leftArm.lock();
}

std::shared_ptr<BaseCube> Humanoid::getRightArmPart() const {
    return m_rightArm.lock();
}

std::shared_ptr<BaseCube> Humanoid::getLeftLegPart() const {
    return m_leftLeg.lock();
}

std::shared_ptr<BaseCube> Humanoid::getRightLegPart() const {
    return m_rightLeg.lock();
}

void Humanoid::setRootPart(const std::shared_ptr<BaseCube>& root) {
    m_root = root;
    if (!root) return;

    // RootはX/Z軸の回転をロックし、転倒しないようにする（Y軸回転=向き変えのみ許可）。
    root->LockFlags = PhysicsLockFlags::AngularX | PhysicsLockFlags::AngularZ;
    // アクター生成後に呼ばれた場合（保存済みシーンのNPC等、遅延resolveParts）でも
    // 転倒防止が効くよう、既存アクターへ直接反映する
    if (root->lastWorkspace && root->lastWorkspace->getPhysicsEngine())
        root->lastWorkspace->getPhysicsEngine()->applyLockFlags(*root);
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

void Humanoid::updateDeath(float dt, Physics* physics) {
    if (!m_dead) return;

    enterRagdoll(physics);
    m_deathElapsed += dt;
}

bool Humanoid::isRespawnReady() const {
    return m_dead && m_deathElapsed >= RespawnTime;
}

void Humanoid::enterRagdoll(Physics* physics) {
    auto root = getRootPart();
    if (!root) {
        if (auto parent = Parent.lock()) resolveParts(parent.get());
        root = getRootPart();
    }
    if (m_ragdollEntered) return;
    m_ragdollEntered = true;

    stopAnimation();

    // -1..1 の乱数
    auto frand = []() { return (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f; };

    // Root は不可視の物理ボディ。散乱に干渉しないよう衝突を無効化してアクターを外す
    if (root) {
        root->CanCollide = false;
        if (physics) physics->recreateActor(root);
    }

    // 各ボディパーツを動的アクター化してランダムに吹き飛ばす
    std::shared_ptr<BaseCube> parts[] = {
        getTorsoPart(),
        getHeadPart(),
        getLeftArmPart(),
        getRightArmPart(),
        getLeftLegPart(),
        getRightLegPart(),
    };
    for (auto& part : parts) {
        if (!part) continue;
        part->CanCollide = true;
        part->Anchored   = false;
        part->LockFlags  = PhysicsLockFlags::None; // 自由に回転させる
        if (!physics) continue;
        physics->recreateActor(part);
        float speed = 8.0f + (static_cast<float>(std::rand()) / RAND_MAX) * 7.0f; // 8..15
        physics->setLinearVelocity(*part,
            Vector3(frand() * speed, 6.0f + frand() * 4.0f, frand() * speed));
        physics->setAngularVelocity(*part,
            Vector3(frand() * 10.0f, frand() * 10.0f, frand() * 10.0f));
    }
}

void Humanoid::resolveParts(Instance* characterModel) {
    if (!characterModel) return;
    const auto& kids = characterModel->getChildren();

    auto find = [&kids](const char* name) -> std::shared_ptr<Instance> {
        auto it = kids.find(name);
        return (it != kids.end()) ? it->second : nullptr;
    };

    setRootPart(std::dynamic_pointer_cast<BaseCube>(find("Root")));
    m_torso    = std::dynamic_pointer_cast<BaseCube>(find("Torso"));
    m_head     = std::dynamic_pointer_cast<BaseCube>(find("Head"));
    m_leftArm  = std::dynamic_pointer_cast<BaseCube>(find("LeftArm"));
    m_rightArm = std::dynamic_pointer_cast<BaseCube>(find("RightArm"));
    m_leftLeg  = std::dynamic_pointer_cast<BaseCube>(find("LeftLeg"));
    m_rightLeg = std::dynamic_pointer_cast<BaseCube>(find("RightLeg"));

    // RootはX/Z軸の回転をロックし、転倒しないようにする（Y軸回転=向き変えのみ許可）。
    // ユーザーがStarterCharacter内に独自のCubeを"Root"として置いた場合も同じ挙動にするため、
    // 物理アクター生成前のここで毎回設定する。実処理はsetRootPart()に集約する
}

void Humanoid::move(const Vector3& flatForward, const Vector3& flatRight, bool isPressingMove,
                     const Vector3& targetMoveDir, bool ctrlLockEnabled, Physics* physics,
                     bool leftArmRaised, bool rightArmRaised,
                     float forwardAxis, float rightAxis, float deltaTime) {
    if (m_dead) return;
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root)) return;

    // --- Seat: 未着席なら接触判定、着席中ならSteer/Throttle更新のみ行って抜ける ---
    if (!m_seated && physics) {
        if (BaseCube* seatCube = physics->findOverlapping(*root, "Seat")) {
            auto seat = std::static_pointer_cast<Seat>(seatCube->shared_from_this());
            if (!seat->isOccupied())
                sitOn(seat, physics);
        }
    }
    if (m_seated) {
        if (auto seat = m_seat.lock()) {
            seat->Throttle = forwardAxis;
            seat->Steer    = rightAxis;
        }
        applyBodyAnimation(leftArmRaised, rightArmRaised);
        return;
    }

    const float frameScale = std::max(deltaTime, 0.0f) * 60.0f;
    const float smoothingAlpha = 1.0f - std::pow(1.0f - 0.15f, frameScale);

    // --- 移動ベクトルの補間 ---
    currentMoveDir = currentMoveDir + (targetMoveDir - currentMoveDir) * smoothingAlpha;

    // --- Truss(はしご)接触判定。登坂中は重力を切り、静止していても留まれるようにする ---
    BaseCube* trussCube = physics ? physics->findOverlapping(*root, "Truss", 0.5f) : nullptr;
    physics->setGravityEnabled(*root, trussCube == nullptr);

    // --- 向き(Rotation)の更新 ---
    // Truss接触中は向きを固定する(自動回転させると登坂中に姿勢が崩れて落下してしまうため)
    if (!trussCube) {
        Quaternion targetRot = root->Rotation;
        if (ctrlLockEnabled) {
            // CtrlLock中は常にカメラの正面方向を向く（Roblox ShiftLock方式）
            targetRot = Quaternion::LookRotation(flatForward, Vector3(0, 1, 0));
        } else if (isPressingMove) {
            targetRot = Quaternion::LookRotation(targetMoveDir, Vector3(0, 1, 0));
        }
        root->Rotation = Quaternion::Slerp(root->Rotation, targetRot, smoothingAlpha);

        CFrame bodyCFrame = physics->getBodyWorldCFrame(*root);
        bodyCFrame.Rotation = root->Rotation;
        physics->setBodyWorldCFrame(*root, bodyCFrame);
    }

    // --- 物理速度の適用 ---
    if (trussCube) {
        // Truss(はしご)接触中: W/Sで垂直方向、A/Dで水平ストレイフ
        Vector3 climbVel = flatRight * (rightAxis * WalkSpeed);
        climbVel.y = forwardAxis * ClimbSpeed;
        physics->setLinearVelocity(*root, climbVel);
    } else if (currentMoveDir.length() > 0.01f) {
        Vector3 velocity = currentMoveDir * WalkSpeed;
        Vector3 currentVel = physics->getLinearVelocity(*root);
        physics->setLinearVelocity(*root, Vector3(velocity.x, currentVel.y, velocity.z));
    } else {
        Vector3 currentVel = physics->getLinearVelocity(*root);
        physics->setLinearVelocity(*root, Vector3(0, currentVel.y, 0));
    }

    // --- アニメーションサイクル（0.0 ~ 1.0）の更新 ---
    const float animationStep = 0.025f * frameScale;
    if (isPressingMove) {
        walkCycle = std::fmod(walkCycle + animationStep, 1.0f);
        if (walkCycle < 0.0f) walkCycle += 1.0f;
    } else if (walkCycle > 0.0f) {
        if (walkCycle > 0.5f) {
            walkCycle = std::min(1.0f, walkCycle + animationStep);
            if (walkCycle >= 1.0f) walkCycle = 0.0f;
        } else {
            walkCycle = std::max(0.0f, walkCycle - animationStep);
        }
    }

    // --- 地面判定 ---
    RaycastHit hit;
    float maxDist = (root->Size.y / 2.0f) + 0.2f;
    isGrounded = physics->raycast(
        root->getWorldPosition(), Vector3(0, -1, 0), maxDist, hit, root.get());

    applyBodyAnimation(leftArmRaised, rightArmRaised);
}

bool Humanoid::moveToward(const Vector3& target, Physics* physics, float deltaTime,
                          float arrivalRadius) {
    if (m_dead) return false;
    auto root = getRootPart();
    if (!root) {
        resolveParts(Parent.lock().get());
        root = getRootPart();
    }
    if (!root) return false;

    Vector3 toTarget = target - root->getWorldPosition();
    toTarget.y = 0.0f;
    float dist = toTarget.length();
    if (dist <= arrivalRadius) {
        move(Vector3(0, 0, -1), Vector3(1, 0, 0), false, Vector3(0, 0, 0), false,
             physics, false, false, 0.0f, 0.0f, deltaTime);
        return true;
    }

    Vector3 dir = toTarget.normalize();
    move(dir, Vector3::Cross(Vector3(0, 1, 0), dir), true, dir, false,
         physics, false, false, 0.0f, 0.0f, deltaTime);
    return false;
}

float Humanoid::getJumpHeight() const {
    float g = METER_TO_STUD * EARTH_GRAVITY_MPS2;
    return (JumpPower * JumpPower) / (2.0f * g);
}

void Humanoid::setJumpHeight(float height) {
    float g = METER_TO_STUD * EARTH_GRAVITY_MPS2;
    JumpPower = std::sqrt(2.0f * g * std::max(height, 0.0f));
}

void Humanoid::jump(Physics* physics) {
    if (m_dead) return;
    auto root = getRootPart();
    if (!root) {
        resolveParts(Parent.lock().get());
        root = getRootPart();
    }
    if (!root || !physics || !physics->hasBody(*root)) return;
    bool submerged = physics && physics->findOverlapping(*root, "LiquidCube") != nullptr;
    if (!isGrounded && !submerged) return;
    Vector3 vel = physics->getLinearVelocity(*root);
    vel.y = JumpPower;
    physics->setLinearVelocity(*root, vel);
}

void Humanoid::sitOn(std::shared_ptr<Seat> seat, Physics* physics) {
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root) || !seat) return;

    seat->setOccupant(std::static_pointer_cast<Humanoid>(shared_from_this()));

    // RootをSeatの向きのまま直上へスナップし、速度をゼロクリアしてからWeldで固定する。
    // Decal.FrontはCubeローカル+Z面(Renderer_GUI.cpp参照)だが、Humanoidの正面(getForward)は-Z基準のため、
    // Seatの回転をそのまま使うとFrontとは逆の-Z方向を向いてしまう。180度反転して整合させる
    CFrame target = seat->getWorldCFrame() * CFrame(0, seat->Size.y * 0.001f - root->Size.y * 0.01f, 0)
                  * CFrame::fromAxisAngle(Vector3(0, 1, 0), 0.0f); // やっぱり必要なさそうなので0.0にした
    physics->setBodyWorldCFrame(*root, target);
    physics->setLinearVelocity(*root, Vector3());
    physics->setAngularVelocity(*root, Vector3());
    physics->syncCube(*root);

    // 着席中はRootの姿勢をSeatWeldが保持するため、徒歩用の転倒防止ロックは不要かつ有害。
    // 外さないと Weld によるcompound化(rebuildGroup)でこのロックが車両アセンブリ全体に
    // OR合成され、車両が傾いた際にロックと重力/接触トルクが衝突して振動・座席位置ずれを起こす
    root->LockFlags = PhysicsLockFlags::None;

    Instance* wsRaw = seat->findFirstAncestorWorkspace();
    if (wsRaw && physics) {
        m_seatWeld = std::make_shared<Weld>();
        m_seatWeld->Name = "SeatWeld";
        static_cast<Workspace*>(wsRaw)->addChild(m_seatWeld);
        m_seatWeld->setCube0(root);
        m_seatWeld->setCube1(seat);

        // Weldの実際のcompound化(Physics::createWeld)は本来次フレームのPhysics::update()
        // 冒頭まで遅延される。その間にも物理シミュレーションが1ステップ以上進んでしまい、
        // まだ独立した自由な剛体のRootが重力等でスナップ直後の姿勢から動いてしまうと、
        // そのズレた姿勢がそのまま車両アセンブリの原点として焼き付き、Seat/Chassis/車輪
        // すべてが恒久的にずれる。スナップした直後のこのフレームのうちに同期的にWeldを
        // 確定させることで、その空白窓を無くす
        Workspace* ws = static_cast<Workspace*>(wsRaw);
        physics->createWeld(m_seatWeld, *ws);

        // setCube1()のregisterIfReady()がすでにこのWeldをpendingConstraintsへ積んでいる。
        // 消さずに残すと次フレームのPhysics::update()冒頭で同じWeldに対しcreateWeldが
        // 二重に走り、車輪Motor(assembly内のcubeを参照しているため巻き添えで再構築される)
        // まで無駄にもう一度作り直されてしまう。二重処理を避けるためここで取り除く
        auto& pending = ws->pendingConstraints;
        pending.erase(std::remove(pending.begin(), pending.end(), m_seatWeld), pending.end());
    }

    m_seated = true;
    m_seat   = seat;
}

void Humanoid::standUp(Physics* physics) {
    if (!m_seated) return;

    auto root = getRootPart();

    // SeatWeld除去(compound分割)前に転倒防止ロックを復帰させる。分割後にRootが
    // 独立アクターとして再生成される際(createActor)、このLockFlagsが反映されるため
    if (root) {
        // ここではまだSeatのcompound actorを共有しているため、actorへ直接適用しない
        root->LockFlags = PhysicsLockFlags::AngularX | PhysicsLockFlags::AngularZ;
    }

    if (m_seatWeld) {
        if (auto parent = m_seatWeld->Parent.lock()) parent->removeChild(m_seatWeld->Name);
        m_seatWeld.reset();
    }

    // 降りるホップ + シートから離れて再着席ループを防ぐ
    if (root && physics && physics->hasBody(*root)) {
        Vector3 vel = physics->getLinearVelocity(*root);
        vel.y = JumpPower;
        physics->setLinearVelocity(*root, vel);
        CFrame bodyCFrame = physics->getBodyWorldCFrame(*root);
        bodyCFrame.Position.y += root->Size.y;
        physics->setBodyWorldCFrame(*root, bodyCFrame);
        physics->syncCube(*root);
    }

    if (auto seat = m_seat.lock()) seat->clearOccupant();
    m_seat.reset();
    m_seated = false;
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
    // このフレームでapplyBodyAnimation()が呼ばれたかを読み取り、即座に消費する。
    // Animation非再生中(早期return)のフレームをまたいでも古い値が残らないよう、
    // 早期return分岐より前で無条件に行う
    bool bodyPoseUpdated = m_bodyPoseUpdatedThisFrame;
    m_bodyPoseUpdatedThisFrame = false;

    if (!m_animPlaying || !m_currentAnim) return;

    // 対象パーツの解決先となるModel(=このHumanoidの親)
    Instance* model = Parent.lock().get();
    if (!model) return;

    // move()/moveToward()/jump()が一度も呼ばれないHumanoid(例: StarterCharacterテンプレートに
    // PlayAnimationだけを呼ぶスクリプトを置いた場合)はRootが永久に未解決のままになる。
    // moveToward()/jump()と同じ遅延解決パターンをここでも踏襲する
    auto root = getRootPart();
    if (!root) {
        resolveParts(model);
        root = getRootPart();
    }

    float prevTime = m_animTime;
    m_animTime += dt * m_currentAnim->Speed;
    bool wrapped = false;
    if (m_currentAnim->Length > 1e-6f) {
        if (m_currentAnim->Looped) {
            while (m_animTime > m_currentAnim->Length) {
                m_animTime -= m_currentAnim->Length;
                wrapped = true;
            }
        } else if (m_animTime >= m_currentAnim->Length) {
            m_animTime = m_currentAnim->Length;
            m_animPlaying = false;
        }
    }

    // キーフレーム通過検知: 通過区間(ラップ無し: (prevTime, m_animTime]、ラップ有り:
    // (prevTime, Length] ∪ [0, m_animTime])に含まれるキーフレーム時刻を持つトラックについて
    // KeyframeReachedを発火する（パーツ名と時刻を引数に渡す）
    if (KeyframeReached) {
        float length = m_currentAnim->Length;
        for (const AnimTrack& track : m_currentAnim->getTracks()) {
            for (const Keyframe& kf : track.keyframes) {
                bool passed = wrapped
                    ? ((kf.time > prevTime && kf.time <= length) || (kf.time >= 0.0f && kf.time <= m_animTime))
                    : (kf.time > prevTime && kf.time <= m_animTime);
                if (!passed) continue;
                std::string partName = track.partName;
                float kfTime = kf.time;
                KeyframeReached->fire([partName, kfTime](lua_State* Lx) -> int {
                    lua_pushstring(Lx, partName.c_str());
                    lua_pushnumber(Lx, kfTime);
                    return 2;
                });
            }
        }
    }

    // このフレーム中に(move()等から)applyBodyAnimation()が実際の引数で呼ばれていなければ、
    // トラック未指定パーツ(例: Headだけのカスタムアニメーション時のTorso/Arms/Legs)が
    // シーンYAML読み込み時の生の絶対座標に凍りついたまま分解して見えないよう、
    // アイドルポーズへフォールバックする。move()が毎フレーム呼ばれている間はこちらは発火せず、
    // 既存の歩行/アイドルポーズがそのまま優先される(トラックで上書きされる分は下のループで再上書きされる)
    if (!bodyPoseUpdated) applyBodyAnimation(false, false);

    // キーフレームはRoot相対で保持されているため、現在のRoot CFrameに合成して
    // キャラクターの移動・回転に追従させる（歩行アニメと同じ基準）
    CFrame rootCF = root ? root->cframe : CFrame();
    for (const AnimTrack& track : m_currentAnim->getTracks()) {
        Instance* child = model->getChild(track.partName);
        Spatial* part = dynamic_cast<Spatial*>(child);
        if (!part || part == root.get()) continue; // Rootは物理駆動なので動かさない
        part->cframe = rootCF * m_currentAnim->evaluateTrack(track, m_animTime);
    }
}

void Humanoid::updateFirstPersonState(bool wantsFirstPerson) {
    auto root = getRootPart();
    auto torso = getTorsoPart();
    auto head = getHeadPart();
    auto leftArm = getLeftArmPart();
    auto rightArm = getRightArmPart();
    auto leftLeg = getLeftLegPart();
    auto rightLeg = getRightLegPart();
    if (!root || !torso || !head || !leftArm || !rightArm || !leftLeg || !rightLeg) return;

    if (wantsFirstPerson && !isFirstPerson) {
        if (!bodyColorsSaved) {
            savedTorsoColor    = torso->Color;
            savedHeadColor     = head->Color;
            savedLeftArmColor  = leftArm->Color;
            savedRightArmColor = rightArm->Color;
            savedLeftLegColor  = leftLeg->Color;
            savedRightLegColor = rightLeg->Color;
            bodyColorsSaved = true;
        }
        Color4 hidden = Color4(1.0f, 1.0f, 1.0f, 0.0f);
        torso->Color    = hidden;
        head->Color     = hidden;
        leftArm->Color  = hidden;
        rightArm->Color = hidden;
        leftLeg->Color  = hidden;
        rightLeg->Color = hidden;
        isFirstPerson = true;
    } else if (!wantsFirstPerson && isFirstPerson) {
        if (bodyColorsSaved) {
            torso->Color    = savedTorsoColor;
            head->Color     = savedHeadColor;
            leftArm->Color  = savedLeftArmColor;
            rightArm->Color = savedRightArmColor;
            leftLeg->Color  = savedLeftLegColor;
            rightLeg->Color = savedRightLegColor;
            bodyColorsSaved = false;
        }
        isFirstPerson = false;
    }
}

Vector3 Humanoid::getRootWorldPosition() const {
    auto root = getRootPart();
    return root ? root->getWorldPosition() : Vector3(0, 0, 0);
}

Vector3 Humanoid::getHeadWorldPosition() const {
    auto head = getHeadPart();
    if (head) return head->getWorldPosition();
    auto root = getRootPart();
    return root ? root->getWorldPosition() : Vector3(0, 0, 0);
}

// ============================================================
// Animation: Pose計算
// ============================================================

Humanoid::Pose Humanoid::computePose(bool leftArmRaised, bool rightArmRaised) const {
    const float PI = 3.14159265f;
    float rad   = walkCycle * 2.0f * PI;
    float swing = std::sin(rad) * 35.0f;

    Pose p;
    if (m_seated) {
        // 着席時のハードコードされたポーズ: 脚は座面に合わせて前方へ折り曲げ、腕は自然に下ろす
        p.leftArm  = 10.0f;
        p.rightArm = 10.0f;
        p.leftLeg  = 90.0f;
        p.rightLeg = 90.0f;
    } else if (leftArmRaised || rightArmRaised) {
        p.leftArm  = leftArmRaised  ? 90.0f : swing;
        p.rightArm = rightArmRaised ? 90.0f : -swing;
        p.leftLeg  = -swing;
        p.rightLeg =  swing;
    } else if (isGrounded) {
        p.leftArm  = swing;
        p.rightArm = -swing;
        p.leftLeg  = -swing;
        p.rightLeg =  swing;
    } else {
        p.leftArm  = 180.0f;
        p.rightArm = 180.0f;
        p.leftLeg  = -swing;
        p.rightLeg =  swing;
    }
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
    m_bodyPoseUpdatedThisFrame = true; // 呼ばれた事実を記録(Root未解決で以降no-opでも「試行済み」として扱う)
    auto root = getRootPart();
    if (!root) {
        if (Instance* model = Parent.lock().get()) resolveParts(model);
        root = getRootPart();
    }
    if (!root) return;

    auto torso = getTorsoPart();
    auto head = getHeadPart();
    auto leftArm = getLeftArmPart();
    auto rightArm = getRightArmPart();
    auto leftLeg = getLeftLegPart();
    auto rightLeg = getRightLegPart();

    Pose pose = computePose(leftArmRaised, rightArmRaised);

    // --- リグ定義（全部ここに固定） ---
    const Vector3 torsoOffset      = Vector3(0, 1.0f, 0);
    const Vector3 headOffset       = Vector3(0, 2.5f, 0);

    const Vector3 leftShoulderPos  = Vector3(-1.5f, 2.0f, 0);
    const Vector3 rightShoulderPos = Vector3( 1.5f, 2.0f, 0);

    const Vector3 leftHipPos       = Vector3(-0.5f, 0.0f, 0);
    const Vector3 rightHipPos      = Vector3( 0.5f, 0.0f, 0);

    if (torso) torso->cframe = root->cframe * CFrame(torsoOffset.x, torsoOffset.y, torsoOffset.z);
    if (head)  head->cframe  = root->cframe * CFrame(headOffset.x,  headOffset.y,  headOffset.z);

    if (leftArm)  leftArm->cframe  = makeArm(root->cframe, leftShoulderPos,  pose.leftArm);
    if (rightArm) rightArm->cframe = makeArm(root->cframe, rightShoulderPos, pose.rightArm);
    if (leftLeg)  leftLeg->cframe  = makeLeg(root->cframe, leftHipPos,  pose.leftLeg);
    if (rightLeg) rightLeg->cframe = makeLeg(root->cframe, rightHipPos, pose.rightLeg);
}

void Humanoid::updateAll(Instance* root, float dt, Physics* physics) {
    if (!root) return;
    if (root->IsA("Humanoid")) {
        auto* humanoid = static_cast<Humanoid*>(root);
        humanoid->updateDeath(dt, physics);
        humanoid->updateAnimation(dt);
    }
    for (auto const& [name, child] : root->getChildren())
        updateAll(child.get(), dt, physics);
}
