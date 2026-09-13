#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
#include <Core/AnimationClip.hpp>
#include <Core/CharacterRig.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Weld.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/Gyro.hpp>
#include <Instances/Force.hpp>
#include <Util/Logger.hpp>
#include <Instances/Motor6D.hpp>
#include <Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <Math/Quaternion.hpp>
#include <Math/CFrame.hpp>
#include <Math/Units.hpp>
#include <include/Util/Logger.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr const char* HOVER_FORCE_NAME = "CharacterHoverForce";

std::shared_ptr<Force> findCharacterYawForce(
    const std::shared_ptr<BaseCube>& root
) {
    if (!root) {
        return nullptr;
    }

    const auto& children = root->getChildren();
    const auto it = children.find("YawForce");

    if (it == children.end()) {
        return nullptr;
    }

    return std::dynamic_pointer_cast<Force>(
        it->second
    );
}

}

std::vector<std::shared_ptr<BaseCube>>
Humanoid::collectCharacterBodies() const {
    auto model = Parent.lock();
    auto bodies = CharacterRig::collectR6Bodies(model.get());
    auto root = getRootPart();
    if (
        root &&
        std::none_of(
            bodies.begin(),
            bodies.end(),
            [&](const auto& body) { return body == root; }
        )
    ) {
        bodies.insert(bodies.begin(), std::move(root));
    }
    return bodies;
}

void Humanoid::setHoverForces(
    Physics* physics,
    bool enabled,
    float acceleration
) {
    for (const auto& body : collectCharacterBodies()) {
        if (!body) continue;
        const auto found = body->getChildren().find(HOVER_FORCE_NAME);
        std::shared_ptr<Force> force;
        if (found != body->getChildren().end()) {
            force = std::dynamic_pointer_cast<Force>(found->second);
            if (!force) {
                RCBN_ERROR(
                    "Humanoid \"" << getFullPath()
                    << "\": reserved hover child \""
                    << body->getFullPath() << '\\' << HOVER_FORCE_NAME
                    << "\" is not a Force"
                );
                continue;
            }
        } else if (enabled) {
            force = std::make_shared<Force>();
            force->Name = HOVER_FORCE_NAME;
            body->addChild(force);
        }
        if (!force) continue;
        if (!enabled || body->Anchored || !physics || !physics->hasBody(*body)) {
            force->Value = {};
            force->Enabled = false;
            continue;
        }
        const auto mass = physics->getBodyMass(*body);
        if (!mass) {
            force->Value = {};
            force->Enabled = false;
            continue;
        }
        force->Torque = false;
        force->MaintainVelocity = false;
        force->Value = Vector3(0.0f, *mass * acceleration, 0.0f);
        force->Enabled = acceleration > 0.0f;
    }
}

void Humanoid::updateGroundHover(
    Physics* physics,
    const std::shared_ptr<BaseCube>& root
) {
    if (!physics || !root || m_dead || m_seated) {
        setHoverForces(physics, false, 0.0f);
        isGrounded = false;
        return;
    }
    const auto& settings = CharacterRig::groundHeightSettings();
    RaycastHit floor;
    auto character = Parent.lock();
    const bool hasFloor = physics->raycast(
        root->getWorldPosition(),
        Vector3(0.0f, -1.0f, 0.0f),
        settings.maxFloorDetectionDistance,
        floor,
        character.get()
    );
    const float verticalVelocity = physics->getLinearVelocity(*root).y;
    isGrounded =
        hasFloor && floor.distance <= settings.landingCaptureDistance;
    if (m_hoverSuppressedForJump) {
        const bool descendingIntoCapture =
            verticalVelocity <= 0.0f && isGrounded;
        if (!descendingIntoCapture) {
            setHoverForces(physics, false, 0.0f);
            return;
        }
        m_hoverSuppressedForJump = false;
    }
    if (!hasFloor) {
        setHoverForces(physics, false, 0.0f);
        return;
    }
    const float gravityCompensation =
        std::max(0.0f, -physics->getGravity().y) *
        settings.gravityCompensationScale;
    const float acceleration = std::clamp(
        gravityCompensation +
            settings.stiffness * (settings.targetDistance - floor.distance) -
            settings.damping * verticalVelocity,
        0.0f,
        settings.maxUpwardAcceleration
    );
    setHoverForces(physics, true, acceleration);
}

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
        custom("WalkAnimation", PropType::String,
            [](Instance* o) { return static_cast<Humanoid*>(o)->getWalkAnimationPath(); },
            [](Instance* o, const PropValue& v) { static_cast<Humanoid*>(o)->setWalkAnimationPath(std::get<std::string>(v)); }).omitEmpty().noEditor(),
        custom("JumpAnimation", PropType::String,
            [](Instance* o) { return static_cast<Humanoid*>(o)->getJumpAnimationPath(); },
            [](Instance* o, const PropValue& v) { static_cast<Humanoid*>(o)->setJumpAnimationPath(std::get<std::string>(v)); }).omitEmpty().noEditor(),
        custom("EquipAnimation", PropType::String,
            [](Instance* o) { return static_cast<Humanoid*>(o)->getEquipAnimationPath(); },
            [](Instance* o, const PropValue& v) { static_cast<Humanoid*>(o)->setEquipAnimationPath(std::get<std::string>(v)); }).omitEmpty().noEditor(),
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
    copy->m_walkAnimation = m_walkAnimation;
    copy->m_jumpAnimation = m_jumpAnimation;
    copy->m_equipAnimation = m_equipAnimation;
    // m_dead / Died / KeyframeReached は複製せず新規（=生存状態・新しいシグナル）
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

static std::string animationPathFromHumanoid(const Humanoid& humanoid,
                                             const std::shared_ptr<Animation>& animation) {
    if (!animation) return {};
    auto character = humanoid.Parent.lock();
    if (!character) return animation->getFullPath();
    for (auto node = animation->Parent.lock(); node; node = node->Parent.lock()) {
        if (node.get() == character.get()) return animation->getPathUpTo(character.get());
    }
    Instance* top = animation.get();
    for (auto node = animation->Parent.lock(); node; node = node->Parent.lock()) top = node.get();
    return animation->getPathUpTo(top);
}

void Humanoid::setWalkAnimation(const std::shared_ptr<Animation>& animation) {
    m_walkAnimation = animation;
    m_walkAnimationPath = animationPathFromHumanoid(*this, animation);
}

void Humanoid::setJumpAnimation(const std::shared_ptr<Animation>& animation) {
    m_jumpAnimation = animation;
    m_jumpAnimationPath = animationPathFromHumanoid(*this, animation);
}

void Humanoid::setEquipAnimation(const std::shared_ptr<Animation>& animation) {
    m_equipAnimation = animation;
    m_equipAnimationPath = animationPathFromHumanoid(*this, animation);
}

void Humanoid::setWalkAnimationPath(const std::string& path) {
    m_walkAnimationPath = path;
    m_walkAnimation.reset();
}

void Humanoid::setJumpAnimationPath(const std::string& path) {
    m_jumpAnimationPath = path;
    m_jumpAnimation.reset();
}

void Humanoid::setEquipAnimationPath(const std::string& path) {
    m_equipAnimationPath = path;
    m_equipAnimation.reset();
}

void Humanoid::remapClonedInstances(const CloneRemap& map) {
    auto remap = [&map](std::weak_ptr<Animation>& reference) {
        auto current = reference.lock();
        if (!current) return;
        auto it = map.find(current.get());
        if (it != map.end()) reference = std::dynamic_pointer_cast<Animation>(it->second);
    };
    remap(m_walkAnimation);
    remap(m_jumpAnimation);
    remap(m_equipAnimation);
}

void Humanoid::resolveAnimationReferences(Instance* characterModel) {
    if (!characterModel) return;
    auto resolve = [characterModel](const std::string& path) -> std::shared_ptr<Animation> {
        if (path.empty()) return nullptr;
        Instance* found = characterModel->getChildByPath(path);
        if (!found) {
            Instance* top = characterModel;
            for (auto parent = characterModel->Parent.lock(); parent; parent = parent->Parent.lock()) top = parent.get();
            found = top->getChildByPath(path);
        }
        if (!found) return nullptr;
        return std::dynamic_pointer_cast<Animation>(found->shared_from_this());
    };
    if (m_walkAnimation.expired()) m_walkAnimation = resolve(m_walkAnimationPath);
    if (m_jumpAnimation.expired()) m_jumpAnimation = resolve(m_jumpAnimationPath);
    if (m_equipAnimation.expired()) m_equipAnimation = resolve(m_equipAnimationPath);
}

const AnimationClip& Humanoid::resolveWalkClip() const {
    static const AnimationClip builtin = AnimationClip::defaultR6Walk();
    auto animation = m_walkAnimation.lock();
    if (!animation) return builtin;
    return animation->resolveR6WalkClip();
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
}

void Humanoid::setRootGyro(const std::shared_ptr<Gyro>& gyro) {
    m_rootGyro = gyro;
}

void Humanoid::setHealth(float v) {
    if (std::isnan(v)) {
        RCBN_ERROR("Received NaN value for Health, rejected.");
        return;
    }

    if (v > MaxHealth) v = MaxHealth;
    bool wasAlive = (Health > 0.0f) && !m_dead;
    Health = v;
    if (Health <= 0.0f && wasAlive) {
        Health = 0.0f;
        m_dead = true;
        setHoverForces(nullptr, false, 0.0f);
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
    setHoverForces(physics, false, 0.0f);
    auto root = getRootPart();
    if (!root) {
        if (auto parent = Parent.lock()) resolveParts(parent.get());
        root = getRootPart();
    }
    if (m_ragdollEntered) return;
    m_ragdollEntered = true;

    stopAnimation();

    if (auto gyro = m_rootGyro.lock()) {
        gyro->setEnabled(false);
    }

    if (auto yawForce = findCharacterYawForce(root)) {
        yawForce->Value.y = 0.0f;
        yawForce->Enabled = false;
    }

    if (auto model = Parent.lock()) {
        for (const auto& topology : CharacterRig::r6JointTopology()) {
            if (auto motor = std::dynamic_pointer_cast<Motor6D>(
                    model->getChildren().contains(topology.jointName)
                        ? model->getChildren().at(topology.jointName) : nullptr))
                motor->setEnabled(false);
            const std::string ragdollName = topology.jointName + "Ragdoll";
            if (auto ragdoll = std::dynamic_pointer_cast<BallSocket>(
                    model->getChildren().contains(ragdollName)
                        ? model->getChildren().at(ragdollName) : nullptr))
                ragdoll->setEnabled(true);
        }
    }

}

void Humanoid::resolveParts(Instance* characterModel) {
    resolveAnimationReferences(characterModel);
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
    m_rootGyro = std::dynamic_pointer_cast<Gyro>(find("RootGyro"));
}

std::shared_ptr<Motor6D> Humanoid::findJointMotor(const std::string& jointName) const {
    auto model = Parent.lock();
    if (!model) return nullptr;
    auto child = model->getChildren().find(jointName);
    return child == model->getChildren().end()
        ? nullptr : std::dynamic_pointer_cast<Motor6D>(child->second);
}

void Humanoid::setJointTransform(const std::string& jointName, const CFrame& transform) {
    const std::string motorName = jointName == "Torso" ? "RootJoint"
        : jointName == "Head" ? "Neck" : jointName;
    if (auto motor = findJointMotor(motorName)) motor->setTransform(transform);
}

void Humanoid::move(const Vector3& flatForward, const Vector3& flatRight, bool isPressingMove,
                     const Vector3& targetMoveDir, bool ctrlLockEnabled, Physics* physics,
                     bool leftArmRaised, bool rightArmRaised,
                     float forwardAxis, float rightAxis, float smoothing, float deltaTime) {
    if (m_dead) {
        setHoverForces(physics, false, 0.0f);
        return;
    }
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root)) {
        setHoverForces(physics, false, 0.0f);
        return;
    }

    auto yawForce =
        findCharacterYawForce(root);

    // --- Seat: 未着席なら接触判定、着席中ならSteer/Throttle更新のみ行って抜ける ---
    if (!m_seated && physics) {
        if (BaseCube* seatCube = physics->findOverlapping(*root, "Seat")) {
            auto seat = std::static_pointer_cast<Seat>(seatCube->shared_from_this());
            if (!seat->isOccupied())
                sitOn(seat, physics);
        }
    }
    if (m_seated) {
        setHoverForces(physics, false, 0.0f);
        if (auto gyro = m_rootGyro.lock()) {
            gyro->setEnabled(false);
        }

        if (yawForce) {
            yawForce->Value.y = 0.0f;
            yawForce->Enabled = false;
        }

        if (auto seat = m_seat.lock()) {
            seat->Throttle = forwardAxis;
            seat->Steer    = rightAxis;
        }

        applyBodyAnimation(
            leftArmRaised,
            rightArmRaised
        );

        return;
    }

    if (yawForce) {
        yawForce->Enabled = true;
    }

    const float frameScale = std::max(deltaTime, 0.0f) * 60.0f;
    const float clampedSmoothing = std::isfinite(smoothing)
        ? std::clamp(smoothing, 0.0f, 1.0f) : 0.15f;
    const float smoothingAlpha = 1.0f - std::pow(1.0f - clampedSmoothing, frameScale);

    // --- 移動ベクトルの補間 ---
    currentMoveDir = currentMoveDir + (targetMoveDir - currentMoveDir) * smoothingAlpha;

    // --- Truss(はしご)接触判定。登坂中は重力を切り、静止していても留まれるようにする ---
    BaseCube* trussCube = physics ? physics->findOverlapping(*root, "Truss", 0.5f) : nullptr;
    physics->setGravityEnabled(*root, trussCube == nullptr);

    // --- 向き(Rotation)の更新 ---
    // Truss接触中は向きを固定する(自動回転させると登坂中に姿勢が崩れて落下してしまうため)
    if (!trussCube) {
        const Vector3* headingDirection = nullptr;

        if (ctrlLockEnabled) {
            headingDirection = &flatForward;
        }
        else if (isPressingMove) {
            headingDirection = &targetMoveDir;
        }

        // X/Z remain physical Gyro axes.
        if (auto gyro = m_rootGyro.lock()) {
            gyro->setEnabled(true);
        }

        if (yawForce) {
            if (
                headingDirection &&
                headingDirection->lengthSquared() > 1e-8f
            ) {
                // @RadiantBird 2026/09/13:
                // Character yaw is intentionally controlled by angular
                // velocity instead of Gyro torque. Pitch and roll remain
                // physical so stopping and acceleration can still make the
                // character lean naturally.
                constexpr float DEGREES_TO_RADIANS =
                    0.01745329251994329577f;

                constexpr float TURN_RESPONSE = 30.0f;
                constexpr float MAX_TURN_SPEED = 20.0f;

                const float targetYaw =
                    Gyro::headingAngleFromDirection(
                        *headingDirection
                    );

                const float currentYaw =
                    Gyro::angleFromRotation(
                        GyroAxis::Y,
                        root->getWorldCFrame().Rotation
                    );

                float errorDegrees =
                    targetYaw - currentYaw;

                while (errorDegrees > 180.0f) {
                    errorDegrees -= 360.0f;
                }

                while (errorDegrees < -180.0f) {
                    errorDegrees += 360.0f;
                }

                const float desiredYawVelocity =
                    std::clamp(
                        errorDegrees *
                            DEGREES_TO_RADIANS *
                            TURN_RESPONSE,
                        -MAX_TURN_SPEED,
                        MAX_TURN_SPEED
                    );

                yawForce->Value.y =
                    desiredYawVelocity;

                // RCBN_LOG(
                //     "Yaw target=" << targetYaw
                //     << " current=" << currentYaw
                //     << " error=" << errorDegrees
                //     << " velocity=" << desiredYawVelocity
                // )

                yawForce->Value.y =
                    desiredYawVelocity;

                // const Vector3 actualAngularVelocity =
                //     physics->getAngularVelocity(*root);

                RCBN_LOG(
                    "Yaw command=" << desiredYawVelocity
                    // << " actualY=" << actualAngularVelocity.y
                    << " error=" << errorDegrees
                );
                
            }
            else {
                yawForce->Value.y = 0.0f;
            }
        }
    }
    else if (yawForce) {
        // Keep the current heading while climbing.
        yawForce->Value.y = 0.0f;
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

    if (trussCube) {
        setHoverForces(physics, false, 0.0f);
        isGrounded = false;
    } else {
        updateGroundHover(physics, root);
    }

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
             physics, false, false, 0.0f, 0.0f, 0.15f, deltaTime);
        return true;
    }

    Vector3 dir = toTarget.normalize();
    move(dir, Vector3::Cross(Vector3(0, 1, 0), dir), true, dir, false,
         physics, false, false, 0.0f, 0.0f, 0.15f, deltaTime);
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
    if (m_dead || !physics) {
        return;
    }

    auto root = getRootPart();

    if (!root) {
        resolveParts(Parent.lock().get());
        root = getRootPart();
    }

    if (
        !root ||
        !physics->hasBody(*root)
    ) {
        return;
    }

    const bool submerged =
        physics->findOverlapping(
            *root,
            "LiquidCube"
        ) != nullptr;

    if (
        !isGrounded &&
        !submerged
    ) {
        return;
    }

    m_hoverSuppressedForJump = true;
    setHoverForces(physics, false, 0.0f);

    // @RadiantBird 2026/09/13:
    // Character Rig v2 uses independent physical bodies connected by Motor6D.
    // Launching only Root makes the joints drag the rest of the character
    // upward from one point, which introduces unwanted rotation.
    //
    // Give every primary R6 body the same vertical launch velocity instead.
    // Preserve each body's existing horizontal velocity.
    auto applyJumpVelocity =
        [&](const std::shared_ptr<BaseCube>& part) {
            if (
                !part ||
                !physics->hasBody(*part)
            ) {
                return;
            }

            Vector3 velocity =
                physics->getLinearVelocity(
                    *part
                );

            velocity.y =
                JumpPower;

            physics->setLinearVelocity(
                *part,
                velocity
            );
        };

    for (const auto& body : collectCharacterBodies()) {
        applyJumpVelocity(body);
    }
}

void Humanoid::sitOn(std::shared_ptr<Seat> seat, Physics* physics) {
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root) || !seat) return;

    seat->setOccupant(std::static_pointer_cast<Humanoid>(shared_from_this()));
    if (auto gyro = m_rootGyro.lock()) gyro->setEnabled(false);

    // RootをSeatの向きのまま直上へスナップし、速度をゼロクリアしてからWeldで固定する。
    // Decal.FrontはCubeローカル+Z面(Renderer_GUI.cpp参照)だが、Humanoidの正面(getForward)は-Z基準のため、
    // Seatの回転をそのまま使うとFrontとは逆の-Z方向を向いてしまう。180度反転して整合させる
    CFrame target = seat->getWorldCFrame() * CFrame(0, seat->Size.y * 0.001f - root->Size.y * 0.01f, 0)
                  * CFrame::fromAxisAngle(Vector3(0, 1, 0), 0.0f); // やっぱり必要なさそうなので0.0にした
    physics->moveWeldAssembly(root, target);
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

        // Weld は Physics の pending transaction で確定する。body pose を直接
        // 操作したり pending リストを手動編集したりせず、登録経路を一つにする。
        // Workspace の pending constraint queue が次の安全窓で一度だけ処理する。
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
        CFrame target = root->getWorldCFrame();
        target.Position.y += root->Size.y;
        physics->moveWeldAssembly(root, target);
        Vector3 vel = physics->getLinearVelocity(*root);
        vel.y = JumpPower;
        physics->setLinearVelocity(*root, vel);
    }

    if (auto seat = m_seat.lock()) seat->clearOccupant();
    m_seat.reset();
    m_seated = false;
    if (auto gyro = m_rootGyro.lock()) {
        if (root) {
            gyro->setCharacterRotation(
                root->getWorldCFrame().Rotation
            );
        }
        gyro->setEnabled(true);
    }
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
        auto notify = [&](const std::string& partName, float kfTime) {
                bool passed = wrapped
                    ? ((kfTime > prevTime && kfTime <= length) || (kfTime >= 0.0f && kfTime <= m_animTime))
                    : (kfTime > prevTime && kfTime <= m_animTime);
                if (!passed) return;
                KeyframeReached->fire([partName, kfTime](lua_State* Lx) -> int {
                    lua_pushstring(Lx, partName.c_str());
                    lua_pushnumber(Lx, kfTime);
                    return 2;
                });
        };
        for (const auto& track : m_currentAnim->getClip()->tracks)
            for (const auto& kf : track.keyframes) notify(track.targetName, kf.time);
    }

    // このフレーム中に(move()等から)applyBodyAnimation()が実際の引数で呼ばれていなければ、
    // トラック未指定パーツ(例: Headだけのカスタムアニメーション時のTorso/Arms/Legs)が
    // シーンYAML読み込み時の生の絶対座標に凍りついたまま分解して見えないよう、
    // アイドルポーズへフォールバックする。move()が毎フレーム呼ばれている間はこちらは発火せず、
    // 既存の歩行/アイドルポーズがそのまま優先される(トラックで上書きされる分は下のループで再上書きされる)
    if (!bodyPoseUpdated) applyBodyAnimation(false, false);

    // キーフレームはRoot相対で保持されているため、現在のRoot CFrameに合成して
    // キャラクターの移動・回転に追従させる（歩行アニメと同じ基準）
    if (m_currentAnim->getClip() && m_currentAnim->getClip()->space == "joint_delta") {
        for (const auto& track : m_currentAnim->getClip()->tracks) {
            const auto* binding = CharacterRig::findR6Joint(track.targetName);
            if (!binding || !root) continue;
            setJointTransform(track.targetName,
                              m_currentAnim->getClip()->evaluate(track, m_animTime));
        }
        return;
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
        p.leftArm = swing; p.rightArm = -swing; p.leftLeg = -swing; p.rightLeg = swing;
        const AnimationClip& walkClip = resolveWalkClip();
        const float t = walkCycle * walkClip.length;
        auto angle = [&](const char* joint, float fallback) {
            const auto* track = walkClip.findTrack(joint);
            if (!track) return fallback;
            const CFrame cf = walkClip.evaluate(*track, t);
            // R6 walk keys are rotation-only X-axis deltas.
            return cf.Rotation.toEuler().x;
        };
        p.leftArm = angle("LeftShoulder", p.leftArm);
        p.rightArm = angle("RightShoulder", p.rightArm);
        p.leftLeg = angle("LeftHip", p.leftLeg);
        p.rightLeg = angle("RightHip", p.rightLeg);
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
    const AnimationClip& walkClip = resolveWalkClip();
    auto clipDelta = [&](const char* joint, float fallback) {
        if (const auto* track = walkClip.findTrack(joint))
            return walkClip.evaluate(*track, walkCycle * walkClip.length);
        return CFrame::fromAxisAngle(Vector3(1,0,0), fallback);
    };

    auto apply = [&](const char* joint, const std::shared_ptr<BaseCube>& part, float angle, const CFrame* direct = nullptr) {
        if (part) setJointTransform(joint,
            direct ? *direct : CFrame::fromAxisAngle(Vector3(1,0,0), angle));
    };
    if (torso) setJointTransform("RootJoint", CFrame());
    if (head) setJointTransform("Neck", CFrame());
    CFrame leftArmDelta = clipDelta("LeftShoulder", pose.leftArm);
    CFrame rightArmDelta = clipDelta("RightShoulder", pose.rightArm);
    CFrame leftLegDelta = clipDelta("LeftHip", pose.leftLeg);
    CFrame rightLegDelta = clipDelta("RightHip", pose.rightLeg);
    // Seat and tool poses override only their relevant joints; airborne poses
    // retain the walking leg cycle while raising both arms.
    if (m_seated) {
        leftArmDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 10.0f);
        rightArmDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 10.0f);
        leftLegDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 90.0f);
        rightLegDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 90.0f);
    } else if (!isGrounded) {
        leftArmDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 180.0f);
        rightArmDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 180.0f);
    }
    // Priority is walk/idle -> jump -> seat -> equip. Explicit Animation tracks
    // are evaluated later by updateAnimation() and override only named joints.
    if (leftArmRaised) leftArmDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 90.0f);
    if (rightArmRaised) rightArmDelta = CFrame::fromAxisAngle(Vector3(1,0,0), 90.0f);
    apply("LeftShoulder", leftArm, pose.leftArm, &leftArmDelta); apply("RightShoulder", rightArm, pose.rightArm, &rightArmDelta);
    apply("LeftHip", leftLeg, pose.leftLeg, &leftLegDelta); apply("RightHip", rightLeg, pose.rightLeg, &rightLegDelta);
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
