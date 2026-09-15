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
#include <limits>

namespace {

constexpr const char* HOVER_FORCE_NAME = "CharacterHoverForce";
constexpr const char* CLIMB_FORCE_NAME = "CharacterClimbForce";
constexpr float RADIANS_TO_DEGREES = 180.0f / 3.14159265f;
constexpr float RECOVERY_ENTRY_ANGULAR_SPEED = 2.0f;
constexpr float RECOVERY_NO_SUPPORT_ENTRY_GRACE = 0.75f;
constexpr float RECOVERY_UPRIGHT_ERROR_DEGREES = 15.0f;
constexpr float RECOVERY_ANGULAR_SPEED = 1.5f;
constexpr float RECOVERY_UPRIGHT_SETTLE_TIME = 0.1f;
constexpr float RECOVERY_GYRO_TIMEOUT = 0.5f;
constexpr float RECOVERY_FALLBACK_LINEAR_SPEED = 4.0f;
constexpr float RECOVERY_FALLBACK_ANGULAR_SPEED = 2.5f;
constexpr float RECOVERY_TIMEOUT = 1.25f;
constexpr float ROOT_SUPPORT_FOOTPRINT_MARGIN = 0.15f;
constexpr float ROOT_SUPPORT_SCAN_START_OFFSET = 0.25f;
constexpr float ROOT_SUPPORT_SCAN_BELOW_FOOT = 1.0f;
constexpr float TRUSS_DESCENT_EXIT_MARGIN = 0.2f;
constexpr float YAW_PROJECTION_EPSILON_SQUARED = 1.0e-8f;

std::uint64_t g_humanoidUpdateAllInvocation = 0;
std::uint32_t g_humanoidUpdateAllDepth = 0;
std::uint64_t g_currentHumanoidUpdateAllInvocation = 0;

bool tryGetHorizontalYaw(
    const Quaternion& rotation,
    float& yawDegrees
) {
    const auto tryDirection = [&](const Vector3& direction) {
        const Vector3 horizontal(direction.x, 0.0f, direction.z);
        if (horizontal.lengthSquared() <= YAW_PROJECTION_EPSILON_SQUARED) {
            return false;
        }
        yawDegrees = Gyro::headingAngleFromDirection(horizontal);
        return std::isfinite(yawDegrees);
    };

    if (tryDirection(rotation.getForward())) {
        return true;
    }
    return tryDirection(rotation.getRight());
}

float uprightErrorDegrees(const Quaternion& rotation) {
    const Vector3 up = rotation.getUp();
    const float upLength = up.length();
    if (!std::isfinite(upLength) || upLength <= 1.0e-6f) {
        return 180.0f;
    }
    const float normalizedUpY = std::clamp(up.y / upLength, -1.0f, 1.0f);
    return std::acos(normalizedUpY) * RADIANS_TO_DEGREES;
}

float wrappedAngleDifferenceDegrees(float first, float second) {
    float difference = first - second;
    while (difference > 180.0f) difference -= 360.0f;
    while (difference < -180.0f) difference += 360.0f;
    return difference;
}

float bodyBottomYAtWorldFrame(
    const BaseCube& body,
    const CFrame& worldFrame
) {
    const Vector3 halfSize = body.Size * 0.5f;
    const Vector3 axisX = worldFrame.Rotation.rotate(
        Vector3(halfSize.x, 0.0f, 0.0f));
    const Vector3 axisY = worldFrame.Rotation.rotate(
        Vector3(0.0f, halfSize.y, 0.0f));
    const Vector3 axisZ = worldFrame.Rotation.rotate(
        Vector3(0.0f, 0.0f, halfSize.z));
    const float verticalExtent =
        std::abs(axisX.y) + std::abs(axisY.y) + std::abs(axisZ.y);
    return worldFrame.Position.y - verticalExtent;
}

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

void Humanoid::setCharacterGravity(Physics* physics, bool enabled) {
    if (!physics) {
        return;
    }

    for (const auto& body : collectCharacterBodies()) {
        if (!body || !physics->hasBody(*body)) {
            continue;
        }
        physics->setGravityEnabled(*body, enabled);
    }
}

void Humanoid::setClimbForces(
    Physics* physics,
    bool enabled,
    const Vector3& targetVelocity
) {
    for (const auto& body : collectCharacterBodies()) {
        if (!body) {
            continue;
        }

        const auto found = body->getChildren().find(CLIMB_FORCE_NAME);
        std::shared_ptr<Force> force;
        if (found != body->getChildren().end()) {
            force = std::dynamic_pointer_cast<Force>(found->second);
            if (!force) {
                RCBN_ERROR(
                    "Humanoid \"" << getFullPath()
                    << "\": reserved climb child \""
                    << body->getFullPath() << '\\' << CLIMB_FORCE_NAME
                    << "\" is not a Force"
                );
                continue;
            }
        } else if (enabled) {
            force = std::make_shared<Force>();
            force->Name = CLIMB_FORCE_NAME;
            body->addChild(force);
        }

        if (!force) {
            continue;
        }

        force->Value = {};
        force->Enabled = false;
        force->Torque = false;
        force->MaintainVelocity = true;

        if (!enabled || body->Anchored || !physics ||
            !physics->hasBody(*body)) {
            continue;
        }

        force->Value = targetVelocity;
        force->Enabled = true;
    }
}

void Humanoid::cancelCharacterDescent(Physics* physics) {
    if (!physics) {
        return;
    }

    for (const auto& body : collectCharacterBodies()) {
        if (!body || !physics->hasBody(*body)) {
            continue;
        }

        Vector3 velocity = physics->getLinearVelocity(*body);
        if (velocity.y >= 0.0f) {
            continue;
        }

        velocity.y = 0.0f;
        physics->setLinearVelocity(*body, velocity);
    }
}

std::optional<float> Humanoid::getLandingImpactEquivalentSpeed(
    Physics* physics,
    float maximumNetUpwardAcceleration,
    float ordinaryCaptureDistance
) const {
    if (!physics) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot measure landing energy without Physics"
        );
        return std::nullopt;
    }
    if (!std::isfinite(maximumNetUpwardAcceleration) ||
        maximumNetUpwardAcceleration < 0.0f ||
        !std::isfinite(ordinaryCaptureDistance) ||
        ordinaryCaptureDistance < 0.0f) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": invalid landing-energy braking parameters acceleration="
            << maximumNetUpwardAcceleration
            << " captureDistance=" << ordinaryCaptureDistance
        );
        return std::nullopt;
    }

    double totalMass = 0.0;
    double verticalKineticEnergy = 0.0;
    for (const auto& body : collectCharacterBodies()) {
        if (!body) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": null character body while measuring landing energy"
            );
            return std::nullopt;
        }
        if (!physics->hasBody(*body)) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": character body \"" << body->getFullPath()
                << "\" has no native body while measuring landing energy"
            );
            return std::nullopt;
        }
        const auto mass = physics->getBodyMass(*body);
        if (!mass) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": cannot read mass for character body \""
                << body->getFullPath()
                << "\" while measuring landing energy"
            );
            return std::nullopt;
        }
        const float verticalVelocity = physics->getLinearVelocity(*body).y;
        if (!std::isfinite(verticalVelocity)) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": non-finite vertical velocity for character body \""
                << body->getFullPath() << "\": " << verticalVelocity
            );
            return std::nullopt;
        }

        const float downwardVelocity = std::min(verticalVelocity, 0.0f);
        totalMass += *mass;
        verticalKineticEnergy += 0.5 * static_cast<double>(*mass) *
            static_cast<double>(downwardVelocity) * downwardVelocity;
    }
    if (!std::isfinite(totalMass) || totalMass <= 0.0 ||
        !std::isfinite(verticalKineticEnergy)) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": invalid aggregate landing energy mass=" << totalMass
            << " verticalKineticEnergy=" << verticalKineticEnergy
        );
        return std::nullopt;
    }

    const double ordinaryCaptureBrakingEnergy = totalMass *
        static_cast<double>(maximumNetUpwardAcceleration) *
        ordinaryCaptureDistance;
    const double residualEnergy = std::max(
        0.0,
        verticalKineticEnergy - ordinaryCaptureBrakingEnergy
    );
    const double equivalentSpeedSquared =
        2.0 * residualEnergy / totalMass;
    if (!std::isfinite(equivalentSpeedSquared) || equivalentSpeedSquared < 0.0) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": invalid landing impact speed squared="
            << equivalentSpeedSquared
        );
        return std::nullopt;
    }
    return static_cast<float>(std::sqrt(equivalentSpeedSquared));
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
    const bool groundedBefore = isGrounded;
    auto character = Parent.lock();
    const float verticalVelocity = physics->getLinearVelocity(*root).y;
    if (!std::isfinite(verticalVelocity)) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": Root vertical velocity is non-finite while updating hover: "
            << verticalVelocity
        );
        setHoverForces(physics, false, 0.0f);
        isGrounded = false;
        return;
    }
    const float downwardSpeed = std::max(0.0f, -verticalVelocity);
    const float gravityMagnitude = std::max(0.0f, -physics->getGravity().y);
    const float maximumNetUpwardAcceleration =
        settings.maxUpwardAcceleration - gravityMagnitude;
    float brakingDistance = 0.0f;
    if (maximumNetUpwardAcceleration > 0.0f) {
        brakingDistance = downwardSpeed * downwardSpeed /
            (2.0f * maximumNetUpwardAcceleration);
    }
    constexpr float PHYSICS_STEP_SECONDS = 1.0f / 60.0f;
    constexpr float CAPTURE_SAFETY_MARGIN = 0.25f;
    const float dynamicLandingCaptureDistance = std::max(
        settings.landingCaptureDistance,
        brakingDistance + downwardSpeed * PHYSICS_STEP_SECONDS +
            CAPTURE_SAFETY_MARGIN
    );
    const float floorDetectionDistance = std::max(
        settings.maxFloorDetectionDistance,
        HipHeight + dynamicLandingCaptureDistance
    );
    const CFrame rootFrame = root->getWorldCFrame();
    const CFrame groundQueryFrame(
        rootFrame.pointToWorld(Vector3(
            0.0f,
            -root->Size.y * 0.5f - settings.groundQueryThickness * 0.5f,
            0.0f
        )),
        rootFrame.Rotation
    );
    ShapeCastHit groundHit;
    const bool hasFloor = physics->shapeCastBox(
        groundQueryFrame,
        Vector3(
            root->Size.x,
            settings.groundQueryThickness,
            root->Size.z
        ),
        Vector3(0.0f, -1.0f, 0.0f),
        floorDetectionDistance,
        groundHit,
        character.get()
    );
    RaycastHit floor;
    if (hasFloor) {
        floor.hit = true;
        floor.distance = rootFrame.Position.y - groundHit.position.y;
        floor.position = groundHit.position;
        floor.normal = groundHit.normal;
        floor.instance = groundHit.instance;
    }
    bool atHipHeight = false;

    const auto logGroundDebug = [&](const char* stage, float hoverAcceleration) {
#ifdef _DEBUG
        const std::uint64_t tick = physics->getSimulationTick();
        if (m_hasGroundDebugTick && m_lastGroundDebugTick == tick) {
            return;
        }
        m_lastGroundDebugTick = tick;
        m_hasGroundDebugTick = true;

        const auto& children = root->getChildren();
        const auto hoverIt = children.find(HOVER_FORCE_NAME);
        const auto hoverForce = hoverIt == children.end()
            ? nullptr
            : std::dynamic_pointer_cast<Force>(hoverIt->second);
        const auto rootPosition = root->getWorldPosition();
        const auto hitPosition = floor.position;
        const auto hitNormal = floor.normal;

        // RCBN_LOG(
        //     "[CharacterGroundDebug] stage=" << stage
        //     << " humanoid=" << getFullPath()
        //     << " humanoidPtr=" << static_cast<const void*>(this)
        //     << " root=" << root->getFullPath()
        //     << " rootPtr=" << static_cast<const void*>(root.get())
        //     << " rootY=" << rootPosition.y
        //     << " hipHeight=" << HipHeight
        //     << " hipHeightExplicit=" << (m_hipHeightExplicitlySet ? 1 : 0)
        //     << " hipHeightInitialized=" << (m_hipHeightInitializedFromGround ? 1 : 0)
        //     << " query=shape-cast-box"
        //     << " castDistance=" << floorDetectionDistance
        //     << " castTravelDistance=" << groundHit.travelDistance
        //     << " queryThickness=" << settings.groundQueryThickness
        //     << " hasFloor=" << (hasFloor ? 1 : 0)
        //     << " floorDistance=" << floor.distance
        //     << " floorY=" << hitPosition.y
        //     << " floorNormalX=" << hitNormal.x
        //     << " floorNormalY=" << hitNormal.y
        //     << " floorNormalZ=" << hitNormal.z
        //     << " floorInstance=" << (floor.instance ? floor.instance->getFullPath() : "<none>")
        //     << " atHipHeight=" << (atHipHeight ? 1 : 0)
        //     << " groundedBefore=" << (groundedBefore ? 1 : 0)
        //     << " groundedAfter=" << (isGrounded ? 1 : 0)
        //     << " jumpSuppressed=" << (m_hoverSuppressedForJump ? 1 : 0)
        //     << " hoverAcceleration=" << hoverAcceleration
        //     << " hoverForceFound=" << (hoverForce ? 1 : 0)
        //     << " hoverEnabled=" << (hoverForce && hoverForce->Enabled ? 1 : 0)
        //     << " hoverValueY=" << (hoverForce ? hoverForce->Value.y : 0.0f)
        // );
#else
        (void)stage;
        (void)hoverAcceleration;
#endif
    };

    if (hasFloor && !m_hipHeightExplicitlySet && !m_hipHeightInitializedFromGround) {
        if (!std::isfinite(floor.distance) || floor.distance < 0.0f) {
            RCBN_WARN(
                "Humanoid \"" << getFullPath()
                << "\": ground detection returned invalid HipHeight distance "
                << floor.distance
            );
        } else {
            // @RadiantBird 2026/09/13:
            // HipHeight preserves the character's intended Root-to-ground distance.
            // Do not derive or overwrite the Root height from the visual body rig.
            HipHeight = floor.distance;
            m_hipHeightInitializedFromGround = true;
        }
    }
    const bool withinLandingCapture = hasFloor &&
        std::abs(floor.distance - HipHeight) <=
            dynamicLandingCaptureDistance;
    const bool enteringAirborneLandingCapture = !groundedBefore &&
        verticalVelocity <= 0.0f && withinLandingCapture;
    atHipHeight = hasFloor &&
        std::abs(floor.distance - HipHeight) <= settings.landingCaptureDistance;
    // HipHeight is the hover setpoint, not a continuously evaluated jump
    // distance.  Use it only to acquire grounded state (and to release jump
    // suppression).  Once acquired, keep grounded through small floor-distance
    // jitter and leave it only when the floor disappears.
    if (m_hoverSuppressedForJump) {
        isGrounded = false;
        const bool descendingIntoCapture =
            verticalVelocity <= 0.0f && withinLandingCapture;
        if (!descendingIntoCapture) {
            setHoverForces(physics, false, 0.0f);
            logGroundDebug("jump-suppressed", 0.0f);
            return;
        }
        m_hoverSuppressedForJump = false;
    }
    if (enteringAirborneLandingCapture) {
        const auto landingImpact = getLandingImpactEquivalentSpeed(
            physics,
            std::max(0.0f, maximumNetUpwardAcceleration),
            settings.landingCaptureDistance
        );
        if (landingImpact && *landingImpact >= ImpactRagdollThreshold) {
            enterRagdoll(physics, *landingImpact);
            isGrounded = false;
            return;
        }
    }
    if (!hasFloor) {
        isGrounded = false;
    } else if (atHipHeight) {
        isGrounded = true;
    }
    if (!hasFloor) {
        setHoverForces(physics, false, 0.0f);
        logGroundDebug("no-floor", 0.0f);
        return;
    }
    const float gravityCompensation =
        std::max(0.0f, -physics->getGravity().y) *
        settings.gravityCompensationScale;
    const float acceleration = std::clamp(
        gravityCompensation +
            settings.stiffness * (HipHeight - floor.distance) -
            settings.damping * verticalVelocity,
        0.0f,
        settings.maxUpwardAcceleration
    );
    setHoverForces(physics, true, acceleration);
    logGroundDebug("hover", acceleration);
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
        method_prop<&Humanoid::getHipHeight, &Humanoid::setHipHeight>("HipHeight", 0, 50, 0.1f)
            .clampLua()
            .serializeIf([](const Instance* object) {
                const auto* humanoid = static_cast<const Humanoid*>(object);
                return humanoid->isHipHeightExplicitlySet() ||
                       humanoid->isHipHeightInitializedFromGround();
            })
            .copyStateWith([](const Instance* source, Instance* destination) {
                static_cast<const Humanoid*>(source)->copyHipHeightStateTo(
                    *static_cast<Humanoid*>(destination));
            }),
        method_prop<&Humanoid::getJumpHeight, &Humanoid::setJumpHeight>("JumpHeight", 0, 50, 0.1f),
        field   <&Humanoid::ImpactRagdollThreshold>(
            "ImpactRagdollThreshold", 0, 1000, 1.0f).clampLua(),
        field   <&Humanoid::RagdollRecoverySpeed>(
            "RagdollRecoverySpeed", 0, 100, 0.1f).clampLua(),
        field   <&Humanoid::RagdollRecoveryDelay>(
            "RagdollRecoveryDelay", 0, 60, 0.1f).clampLua(),
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

void Humanoid::setHipHeight(float height) {
    if (!std::isfinite(height) || height < 0.0f) {
        RCBN_WARN(
            "Humanoid \"" << getFullPath()
            << "\": rejecting invalid HipHeight " << height
        );
        return;
    }
    HipHeight = height;
    m_hipHeightExplicitlySet = true;
}

void Humanoid::copyHipHeightStateTo(Humanoid& destination) const {
    destination.m_hipHeightExplicitlySet = m_hipHeightExplicitlySet;
    destination.m_hipHeightInitializedFromGround = m_hipHeightInitializedFromGround;
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

void Humanoid::restoreRagdollCollision() {
    for (const auto& body : collectCharacterBodies()) {
        if (!body) continue;
        auto saved = std::find_if(
            m_savedCollisionModes.begin(),
            m_savedCollisionModes.end(),
            [&](const auto& entry) { return entry.first.lock() == body; });
        if (saved == m_savedCollisionModes.end()) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": missing saved collision state for \""
                << body->getFullPath() << '\"'
            );
            continue;
        }
        body->setCanCollide(saved->second);
    }
    m_savedCollisionModes.clear();
}

void Humanoid::setMotor6DConstraintsEnabled(bool enabled) {
    auto model = Parent.lock();
    if (!model) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot change Motor6D constraints without a character model"
        );
        return;
    }

    for (const auto& topology : CharacterRig::r6JointTopology()) {
        const auto motorIt = model->getChildren().find(topology.jointName);
        const auto motor = motorIt == model->getChildren().end()
            ? nullptr : std::dynamic_pointer_cast<Motor6D>(motorIt->second);
        if (!motor) {
            RCBN_WARN(
                "Humanoid \"" << getFullPath()
                << "\": missing Motor6D \"" << topology.jointName << '\"'
            );
        }
        if (!motor) continue;
        if (!enabled) {
            motor->setEnabled(false);
            continue;
        }
        motor->setTransform(CFrame());
        motor->setEnabled(true);
    }
}

void Humanoid::setBallSocketConstraintsEnabled(bool enabled) {
    auto model = Parent.lock();
    if (!model) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot change BallSocket constraints without a character model"
        );
        return;
    }

    for (const auto& topology : CharacterRig::r6JointTopology()) {
        const auto ragdollIt = model->getChildren().find(
            topology.jointName + "Ragdoll");
        const auto ragdoll = ragdollIt == model->getChildren().end()
            ? nullptr : std::dynamic_pointer_cast<BallSocket>(ragdollIt->second);
        if (!ragdoll) {
            RCBN_WARN(
                "Humanoid \"" << getFullPath()
                << "\": missing BallSocket \""
                << topology.jointName << "Ragdoll\""
            );
            continue;
        }
        ragdoll->setEnabled(enabled);
    }
}

void Humanoid::setRagdollConstraintsEnabled(bool enabled) {
    if (enabled) {
        setMotor6DConstraintsEnabled(false);
        setBallSocketConstraintsEnabled(true);
        return;
    }
    setBallSocketConstraintsEnabled(false);
    setMotor6DConstraintsEnabled(true);
}

void Humanoid::saveRagdollBindPose() {
    m_savedRagdollBindPoses.clear();

    auto model = Parent.lock();
    auto root = getRootPart();
    if (!model || !root) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot save Ragdoll bind pose without model and Root"
        );
        return;
    }

    const CFrame rootWorldFrame = root->getWorldCFrame();
    std::vector<std::pair<std::string, CFrame>> relativePoses;
    relativePoses.emplace_back("Root", CFrame());
    m_savedRagdollBindPoses.push_back({root, CFrame()});

    const auto findBody = [&model](const std::string& name) {
        const auto found = model->getChildren().find(name);
        if (found == model->getChildren().end()) return std::shared_ptr<BaseCube>();
        return std::dynamic_pointer_cast<BaseCube>(found->second);
    };
    for (const auto& topology : CharacterRig::r6JointTopology()) {
        const auto body = findBody(topology.part1Name);
        if (!body) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": cannot save bind pose; missing body \""
                << topology.part1Name << '"'
            );
            continue;
        }

        CFrame relative;
        const auto parentBody = findBody(topology.part0Name);
        const auto motor = findJointMotor(topology.jointName);
        const auto parentPoseIt = std::find_if(
            relativePoses.begin(), relativePoses.end(),
            [&](const auto& entry) { return entry.first == topology.part0Name; });
        if (parentBody && motor && parentPoseIt != relativePoses.end()) {
            // Transform is deliberately excluded: bind pose means C0/C1 with
            // the animation delta cleared.
            relative = parentPoseIt->second * motor->C0 * motor->C1.inverse();
        } else {
            if (!parentBody) {
                RCBN_ERROR(
                    "Humanoid \"" << getFullPath()
                    << "\": cannot derive bind pose parent \""
                    << topology.part0Name << '"'
                );
            }
            if (!motor) {
                RCBN_ERROR(
                    "Humanoid \"" << getFullPath()
                    << "\": cannot derive bind pose Motor6D \""
                    << topology.jointName << '"'
                );
            }
            relative = rootWorldFrame.inverse() * body->getWorldCFrame();
        }

        relativePoses.emplace_back(topology.part1Name, relative);
        m_savedRagdollBindPoses.push_back({body, relative});
    }
}

void Humanoid::logRagdollRecoveryRigState(
    Physics* physics,
    const char* phase
) const {
    const std::uint64_t tick = physics ? physics->getSimulationTick() : 0;
    // RCBN_LOG(
    //     "[Ragdoll] recovery diagnostic phase=" << (phase ? phase : "unknown")
    //     << " humanoid=\"" << getFullPath() << '"'
    //     << " humanoidPtr=" << static_cast<const void*>(this)
    //     << " physicsPtr=" << static_cast<const void*>(physics)
    //     << " physicsTick=" << tick
    //     << " updateAllInvocation=" << g_currentHumanoidUpdateAllInvocation
    //     << " supportInstance=\""
    //     << (m_recoverySupportInstancePath.empty()
    //             ? "<none>" : m_recoverySupportInstancePath)
    //     << "\" supportY=" << m_recoverySupportY
    //     << " supportNormal=" << m_recoverySupportNormal.toString()
    //     << " hipHeight=" << HipHeight
    // );

    const auto model = Parent.lock();
    const auto logBody = [&](const char* name) {
        std::shared_ptr<BaseCube> body;
        if (model) {
            const auto found = model->getChildren().find(name);
            if (found != model->getChildren().end()) {
                body = std::dynamic_pointer_cast<BaseCube>(found->second);
            }
        }
        if (!body) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": recovery diagnostic body is missing \""
                << name << '"'
            );
            return;
        }
        const CFrame frame = body->getWorldCFrame();
        // RCBN_LOG(
        //     "[Ragdoll] recovery body phase=" << (phase ? phase : "unknown")
        //     << " name=" << name
        //     << " position=" << frame.Position.toString()
        //     << " bottomY=" << bodyBottomYAtWorldFrame(*body, frame)
        // );
    };
    logBody("Root");
    logBody("Torso");
    logBody("Head");
    logBody("LeftLeg");
    logBody("RightLeg");

    if (!model) return;
    for (const auto& topology : CharacterRig::r6JointTopology()) {
        const auto motorIt = model->getChildren().find(topology.jointName);
        const auto ballIt = model->getChildren().find(
            topology.jointName + "Ragdoll");
        const auto motor = motorIt == model->getChildren().end()
            ? nullptr : std::dynamic_pointer_cast<Motor6D>(motorIt->second);
        const auto ball = ballIt == model->getChildren().end()
            ? nullptr : std::dynamic_pointer_cast<BallSocket>(ballIt->second);
        // RCBN_LOG(
        //     "[Ragdoll] recovery constraint phase="
        //     << (phase ? phase : "unknown")
        //     << " joint=" << topology.jointName
        //     << " motorEnabled=" << (motor && motor->Enabled ? 1 : 0)
        //     << " ballSocketEnabled=" << (ball && ball->Enabled ? 1 : 0)
        // );
    }
}

bool Humanoid::hasRagdollSupport(Physics* physics) const {
    if (!physics) return false;
    auto character = Parent.lock();
    if (!character) return false;
    for (const auto& body : collectCharacterBodies()) {
        if (!body || !physics->hasBody(*body)) continue;
        RaycastHit hit;
        const float distance = std::max(0.5f, body->Size.y * 0.5f + 0.75f);
        if (physics->raycast(
                body->getWorldPosition(),
                Vector3(0.0f, -1.0f, 0.0f),
                distance,
                hit,
                character.get())) {
            return true;
        }
    }
    return false;
}

bool Humanoid::findRootSupport(
    Physics* physics,
    float yawDegrees,
    float& supportY,
    Vector3* supportNormal,
    std::string* supportInstancePath
) const {
    supportY = 0.0f;
    if (supportNormal) *supportNormal = Vector3(0.0f, 1.0f, 0.0f);
    if (supportInstancePath) supportInstancePath->clear();
    if (!physics) return false;

    auto character = Parent.lock();
    auto root = getRootPart();
    if (!character || !root || !physics->hasBody(*root)) return false;

    const CFrame rootFrame = root->getWorldCFrame();
    if (!std::isfinite(yawDegrees)) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": Root support scan received invalid yaw " << yawDegrees
        );
        return false;
    }

    const float desiredRootHeight =
        std::isfinite(HipHeight) && HipHeight >= 0.0f
            ? HipHeight
            : std::max(root->Size.y * 0.5f, 0.1f);
    // Start above the expected support plane so a Root that has sunk into the
    // floor can still be recovered by the downward sweep.
    const float scanStartOffset = std::max(
        ROOT_SUPPORT_SCAN_START_OFFSET,
        desiredRootHeight + ROOT_SUPPORT_SCAN_START_OFFSET
    );
    const CFrame scanFrame(
        rootFrame.Position + Vector3(0.0f, scanStartOffset, 0.0f),
        Quaternion::fromEuler(Vector3(0.0f, yawDegrees, 0.0f))
    );
    const Vector3 footprintSize(
        std::max(root->Size.x, 0.1f) +
            ROOT_SUPPORT_FOOTPRINT_MARGIN * 2.0f,
        0.2f,
        std::max(root->Size.z, 0.1f) +
            ROOT_SUPPORT_FOOTPRINT_MARGIN * 2.0f
    );
    const float scanDistance = std::max(
        scanStartOffset + desiredRootHeight +
            ROOT_SUPPORT_SCAN_BELOW_FOOT,
        scanStartOffset + root->Size.y + ROOT_SUPPORT_SCAN_BELOW_FOOT
    );

    ShapeCastHit hit;
    if (!physics->shapeCastBox(
            scanFrame,
            footprintSize,
            Vector3(0.0f, -1.0f, 0.0f),
            scanDistance,
            hit,
            character.get(),
            0.5f)) {
        return false;
    }
    if (!std::isfinite(hit.position.y) || !std::isfinite(hit.normal.y)) {
        return false;
    }

    supportY = hit.position.y;
    if (supportNormal) *supportNormal = hit.normal;
    if (supportInstancePath) {
        *supportInstancePath = hit.instance
            ? hit.instance->getFullPath() : "<none>";
    }
    return true;
}

bool Humanoid::findRagdollRecoverySupport(
    Physics* physics,
    float& supportY,
    Vector3* supportNormal,
    std::string* supportInstancePath
) const {
    auto root = getRootPart();
    if (!root) {
        return false;
    }

    float yawDegrees = m_recoveryYawDegrees;
    if (!m_recoveryYawValid && !tryGetHorizontalYaw(
            root->getWorldCFrame().Rotation,
            yawDegrees)) {
        yawDegrees = m_hasLastValidYaw ? m_lastValidYawDegrees : 0.0f;
    }
    return findRootSupport(
        physics,
        yawDegrees,
        supportY,
        supportNormal,
        supportInstancePath
    );
}

void Humanoid::updateRagdoll(float dt, Physics* physics) {
    if (!physics) return;
    const bool invalidSettings =
        !std::isfinite(ImpactRagdollThreshold) ||
        ImpactRagdollThreshold <= 0.0f ||
        !std::isfinite(RagdollRecoverySpeed) ||
        RagdollRecoverySpeed < 0.0f ||
        !std::isfinite(RagdollRecoveryDelay) ||
        RagdollRecoveryDelay < 0.0f;
    if (invalidSettings) {
        if (!m_invalidRagdollSettingsReported) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": invalid Ragdoll settings threshold="
                << ImpactRagdollThreshold
                << " recoverySpeed=" << RagdollRecoverySpeed
                << " recoveryDelay=" << RagdollRecoveryDelay
            );
            m_invalidRagdollSettingsReported = true;
        }
        return;
    }
    m_invalidRagdollSettingsReported = false;
    if (m_recoveryDiagnosticPendingPhysicsLog) {
        logRagdollRecoveryRigState(physics, "next-physics-update");
        m_recoveryDiagnosticPendingPhysicsLog = false;
    }
    const std::uint64_t physicsTick = physics->getSimulationTick();
    if (m_hasLastRagdollUpdateInvocation &&
        m_lastRagdollUpdateInvocation ==
            g_currentHumanoidUpdateAllInvocation) {
        ++m_ragdollUpdatesThisInvocation;
        RCBN_WARN(
            "[Ragdoll] duplicate updateAll observation humanoid=\""
            << getFullPath() << '"'
            << " humanoidPtr=" << static_cast<const void*>(this)
            << " physicsPtr=" << static_cast<const void*>(physics)
            << " physicsTick=" << physicsTick
            << " updateAllInvocation=" << g_currentHumanoidUpdateAllInvocation
            << " updateCount=" << m_ragdollUpdatesThisInvocation
        );
    } else {
        m_lastRagdollUpdateInvocation = g_currentHumanoidUpdateAllInvocation;
        m_ragdollUpdatesThisInvocation = 1;
        m_hasLastRagdollUpdateInvocation = true;
    }
    if (m_state == State::Normal) {
        float impact = 0.0f;
        for (const auto& body : collectCharacterBodies()) {
            if (!body || !physics->hasBody(*body)) continue;
            impact = std::max(impact, physics->consumeContactImpact(*body));
        }
        if (impact >= ImpactRagdollThreshold) {
            enterRagdoll(physics, impact);
        }
        return;
    }

    if (m_state == State::Recovering) {
        updateRagdollRecovery(dt, physics);
        return;
    }
    if (isClimbing()) {
        return;
    }

    if (m_dead) return;
    auto root = getRootPart();
    if (!root || !physics->hasBody(*root)) {
        m_ragdollStableTime = 0.0f;
        return;
    }

    const Vector3 linearVelocity = physics->getLinearVelocity(*root);
    const Vector3 angularVelocity = physics->getAngularVelocity(*root);
    const float linearSpeed = linearVelocity.length();
    const float angularSpeed = angularVelocity.length();
    const bool lowSpeed = std::isfinite(linearSpeed) &&
        std::isfinite(angularSpeed) &&
        linearSpeed <= RagdollRecoverySpeed &&
        angularSpeed <= RECOVERY_ENTRY_ANGULAR_SPEED;
    const bool hasSupport = hasRagdollSupport(physics);
    if (lowSpeed) {
        const float stableTimeBefore = m_ragdollStableTime;
        m_ragdollStableTime += std::max(dt, 0.0f);
        if (stableTimeBefore <= 0.0f) {
            // RCBN_LOG(
            //     "[Ragdoll] recovery stable started humanoid=\""
            //     << getFullPath() << '\"'
            //     << " humanoidPtr=" << static_cast<const void*>(this)
            //     << " physicsPtr=" << static_cast<const void*>(physics)
            //     << " physicsTick=" << physics->getSimulationTick()
            //     << " updateAllInvocation=" << g_currentHumanoidUpdateAllInvocation
            //     << " updateCount=" << m_ragdollUpdatesThisInvocation
            //     << " support=" << (hasSupport ? 1 : 0)
            // );
        }
    } else {
        m_ragdollStableTime = 0.0f;
    }
    const float noSupportRecoveryDelay =
        RagdollRecoveryDelay + RECOVERY_NO_SUPPORT_ENTRY_GRACE;
    const bool recoveryGate = hasSupport ||
        m_ragdollStableTime >= noSupportRecoveryDelay;
    if (lowSpeed && m_ragdollStableTime >= RagdollRecoveryDelay &&
        recoveryGate) {
        recoverFromRagdoll(physics);
    }
}

void Humanoid::beginRagdollRecovery(Physics* physics) {
    if (m_state != State::Ragdoll || m_dead) return;

    if (!physics) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot begin Ragdoll recovery without Physics"
        );
        return;
    }

    auto root = getRootPart();
    if (!root) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot begin Ragdoll recovery without Root"
        );
        return;
    }

    m_state = State::Recovering;
    m_recoveryElapsedTime = 0.0f;
    m_recoveryGroundedTime = 0.0f;
    m_recoverySupportY = 0.0f;
    m_recoverySupportNormal = Vector3(0.0f, 1.0f, 0.0f);
    m_recoverySupportInstancePath.clear();
    m_recoverySupportValid = false;
    m_recoveryUprightStableTime = 0.0f;
    m_recoveryFallbackStableTime = 0.0f;
    m_recoveryFinalNormalizationApplied = false;
    m_recoveryUprightStableReported = false;
    m_recoveryFallbackReported = false;
    m_recoveryMissingBodyReported = false;
    m_recoveryDiagnosticPendingPhysicsLog = false;
    m_recoveryYawValid = tryGetHorizontalYaw(
        root->getWorldCFrame().Rotation,
        m_recoveryYawDegrees);
    if (m_recoveryYawValid && m_hasLastValidYaw) {
        const Vector3 up = root->getWorldCFrame().Rotation.getUp();
        const float yawJump = std::abs(wrappedAngleDifferenceDegrees(
            m_recoveryYawDegrees,
            m_lastValidYawDegrees));
        if (up.y < -0.5f && yawJump > 150.0f) {
            m_recoveryYawDegrees = m_lastValidYawDegrees;
        }
    }
    if (!m_recoveryYawValid && m_hasLastValidYaw) {
        m_recoveryYawDegrees = m_lastValidYawDegrees;
        m_recoveryYawValid = true;
    }
    if (!m_recoveryYawValid) {
        m_recoveryYawDegrees = 0.0f;
        RCBN_WARN(
            "Humanoid \"" << getFullPath()
            << "\": Ragdoll recovery has no valid horizontal yaw; using 0"
        );
    }

    // BallSocket is disabled before Motor6D is enabled.  This leaves only the
    // normal bind-pose constraint in control while the RootGyro performs the
    // physical upright recovery.
    setRagdollConstraintsEnabled(false);
    setHoverForces(physics, false, 0.0f);
    if (auto yawForce = findCharacterYawForce(root)) {
        yawForce->Value = Vector3();
        yawForce->Enabled = false;
    }
    root->setLockFlags(PhysicsLockFlags::None);

    if (auto gyro = m_rootGyro.lock()) {
        const auto ySettings = gyro->getAxisSettings(GyroAxis::Y);
        m_savedRootGyroYEnabled = ySettings.Enabled;
        m_savedRootGyroYTarget = ySettings.TargetAngle;
        m_savedRootGyroYStateValid = true;

        gyro->setTargetAngle(GyroAxis::X, 0.0f);
        gyro->setTargetAngle(GyroAxis::Y, m_recoveryYawDegrees);
        gyro->setTargetAngle(GyroAxis::Z, 0.0f);
        gyro->setAxisEnabled(GyroAxis::X, true);
        gyro->setAxisEnabled(GyroAxis::Y, true);
        gyro->setAxisEnabled(GyroAxis::Z, true);
        gyro->setEnabled(true);
    } else {
        RCBN_WARN(
            "Humanoid \"" << getFullPath()
            << "\": RootGyro is missing during Ragdoll recovery"
        );
    }

    // RCBN_LOG(
    //     "[Ragdoll] state=Recovering humanoid=\"" << getFullPath()
    //     << "\" yaw=" << m_recoveryYawDegrees
    //     << " uprightThreshold=" << RECOVERY_UPRIGHT_ERROR_DEGREES
    //     << " angularThreshold=" << RECOVERY_ANGULAR_SPEED
    // );
}

void Humanoid::updateRagdollRecovery(float dt, Physics* physics) {
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root)) {
        m_recoveryUprightStableTime = 0.0f;
        if (!m_recoveryMissingBodyReported) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": Root body is unavailable during Ragdoll recovery"
            );
            m_recoveryMissingBodyReported = true;
        }
        return;
    }
    m_recoveryMissingBodyReported = false;
    m_recoveryElapsedTime += std::max(dt, 0.0f);

    const CFrame rootFrame = root->getWorldCFrame();
    const float uprightError = uprightErrorDegrees(rootFrame.Rotation);
    const float pitchError = std::abs(
        Gyro::angleFromRotation(GyroAxis::X, rootFrame.Rotation));
    const float rollError = std::abs(
        Gyro::angleFromRotation(GyroAxis::Z, rootFrame.Rotation));
    const float linearSpeed = physics->getLinearVelocity(*root).length();
    const float angularSpeed = physics->getAngularVelocity(*root).length();
    float supportY = 0.0f;
    Vector3 supportNormal;
    std::string supportInstancePath;
    const bool grounded = findRagdollRecoverySupport(
        physics, supportY, &supportNormal, &supportInstancePath);
    if (grounded) {
        m_recoverySupportY = supportY;
        m_recoverySupportNormal = supportNormal;
        m_recoverySupportInstancePath = supportInstancePath;
        m_recoverySupportValid = true;
        m_recoveryGroundedTime += std::max(dt, 0.0f);
    } else {
        m_recoveryGroundedTime = 0.0f;
    }
    const bool upright = std::isfinite(uprightError) &&
        std::isfinite(pitchError) && std::isfinite(rollError) &&
        std::isfinite(linearSpeed) &&
        std::isfinite(angularSpeed) &&
        uprightError <= RECOVERY_UPRIGHT_ERROR_DEGREES &&
        pitchError <= RECOVERY_UPRIGHT_ERROR_DEGREES &&
        rollError <= RECOVERY_UPRIGHT_ERROR_DEGREES &&
        angularSpeed <= RECOVERY_ANGULAR_SPEED;

    if (!upright) {
        m_recoveryUprightStableTime = 0.0f;
        m_recoveryUprightStableReported = false;
    } else {
        const float stableTimeBefore = m_recoveryUprightStableTime;
        m_recoveryUprightStableTime += std::max(dt, 0.0f);
        if (stableTimeBefore <= 0.0f && !m_recoveryUprightStableReported) {
            m_recoveryUprightStableReported = true;
            // RCBN_LOG(
            //     "[Ragdoll] upright stable humanoid=\"" << getFullPath()
            //     << "\""
            //     << " uprightError=" << uprightError
            //     << " pitchError=" << pitchError
            //     << " rollError=" << rollError
            //     << " angularSpeed=" << angularSpeed
            //     << " recoveryTimer=" << m_recoveryUprightStableTime
            // );
        }

        if (m_recoveryUprightStableTime >= RECOVERY_UPRIGHT_SETTLE_TIME) {
            finalizeRagdollRecovery(physics, "gyro-success");
            return;
        }
    }

    const bool fallbackStable =
        m_recoveryElapsedTime >= RECOVERY_GYRO_TIMEOUT &&
        linearSpeed <= RECOVERY_FALLBACK_LINEAR_SPEED &&
        angularSpeed <= RECOVERY_FALLBACK_ANGULAR_SPEED;
    if (!fallbackStable) {
        m_recoveryFallbackStableTime = 0.0f;
        m_recoveryFallbackReported = false;
    } else {
        const float fallbackTimeBefore = m_recoveryFallbackStableTime;
        m_recoveryFallbackStableTime += std::max(dt, 0.0f);
        if (fallbackTimeBefore <= 0.0f && !m_recoveryFallbackReported) {
            m_recoveryFallbackReported = true;
            // RCBN_LOG(
            //     "[Ragdoll] gyro fallback stable humanoid=\"" << getFullPath()
            //     << "\""
            //     << " uprightError=" << uprightError
            //     << " pitchError=" << pitchError
            //     << " rollError=" << rollError
            //     << " linearSpeed=" << linearSpeed
            //     << " angularSpeed=" << angularSpeed
            //     << " recoveryTimer=" << m_recoveryElapsedTime
            //     << " fallbackTimer=" << m_recoveryFallbackStableTime
            // );
        }

        if (m_recoveryFallbackStableTime >= RECOVERY_UPRIGHT_SETTLE_TIME) {
            finalizeRagdollRecovery(physics, "speed-fallback");
            return;
        }
    }

    if (m_recoveryElapsedTime >= RECOVERY_TIMEOUT) {
        finalizeRagdollRecovery(physics, "timeout-fallback");
    }
}

void Humanoid::finalizeRagdollRecovery(
    Physics* physics,
    const char* recoveryReason
) {
    if (m_state != State::Recovering || m_recoveryFinalNormalizationApplied) {
        return;
    }

    auto root = getRootPart();
    if (!root) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot finalize Ragdoll recovery without Root"
        );
        return;
    }

    const CFrame currentFrame = root->getWorldCFrame();
    float supportY = 0.0f;
    Vector3 supportNormal;
    std::string supportInstancePath;
    if (findRagdollRecoverySupport(
            physics, supportY, &supportNormal, &supportInstancePath)) {
        m_recoverySupportY = supportY;
        m_recoverySupportNormal = supportNormal;
        m_recoverySupportInstancePath = supportInstancePath;
        m_recoverySupportValid = true;
    }
    if (!m_recoverySupportValid) {
        RCBN_WARN(
            "Humanoid \"" << getFullPath()
            << "\": Ragdoll recovery support scan returned no valid support; "
            << "preserving the current Root height"
        );
    }

    const float desiredRootHeight =
        std::isfinite(HipHeight) && HipHeight >= 0.0f
            ? HipHeight
            : std::max(root->Size.y * 0.5f, 0.1f);
    const float currentY = currentFrame.Position.y;
    float targetY = m_recoverySupportValid
        ? std::max(currentY, m_recoverySupportY + desiredRootHeight)
        : currentY;
    const float finalSupportY = m_recoverySupportY;
    float finalYawDegrees = m_recoveryYawDegrees;
    if (!m_recoveryYawValid && !tryGetHorizontalYaw(
            currentFrame.Rotation, finalYawDegrees)) {
        finalYawDegrees = m_hasLastValidYaw ? m_lastValidYawDegrees : 0.0f;
    }

    Vector3 finalPosition = currentFrame.Position;
    finalPosition.y = targetY;
    CFrame finalWorldFrame(
        finalPosition,
        Quaternion::fromEuler(Vector3(0.0f, finalYawDegrees, 0.0f)));

    logRagdollRecoveryRigState(physics, "before-normalization");

    // Both constraints are detached before any body is moved.  In particular,
    // Motor6D must not pull an old ragdoll body back while Root is normalized.
    setBallSocketConstraintsEnabled(false);
    setMotor6DConstraintsEnabled(false);

    struct PendingBodyPose {
        std::shared_ptr<BaseCube> body;
        CFrame worldFrame;
    };
    std::vector<PendingBodyPose> pendingBodyPoses;
    pendingBodyPoses.reserve(m_savedRagdollBindPoses.size());
    float lowestBodyBottomY = std::numeric_limits<float>::infinity();
    for (const auto& savedPose : m_savedRagdollBindPoses) {
        const auto body = savedPose.body.lock();
        if (!body) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": saved Ragdoll bind pose body expired"
            );
            continue;
        }
        const CFrame bodyWorldFrame = finalWorldFrame * savedPose.relativeToRoot;
        lowestBodyBottomY = std::min(
            lowestBodyBottomY,
            bodyBottomYAtWorldFrame(*body, bodyWorldFrame));
        pendingBodyPoses.push_back({body, bodyWorldFrame});
    }

    // HipHeight remains the primary Root-to-support target.  Only a downward
    // bind-pose extent that would penetrate the selected support is corrected,
    // and the correction is upward-only.
    constexpr float RECOVERY_GROUND_CLEARANCE = 0.02f;
    if (m_recoverySupportValid &&
        std::isfinite(lowestBodyBottomY) &&
        lowestBodyBottomY < m_recoverySupportY + RECOVERY_GROUND_CLEARANCE) {
        const float yCorrection =
            m_recoverySupportY + RECOVERY_GROUND_CLEARANCE - lowestBodyBottomY;
        targetY += yCorrection;
        finalPosition.y = targetY;
        finalWorldFrame.Position.y = targetY;
        for (auto& pending : pendingBodyPoses) {
            for (const auto& savedPose : m_savedRagdollBindPoses) {
                if (savedPose.body.lock() == pending.body) {
                    pending.worldFrame = finalWorldFrame * savedPose.relativeToRoot;
                    break;
                }
            }
        }
    }

    const auto setBodyWorldPose = [](const std::shared_ptr<BaseCube>& body,
                                     const CFrame& worldFrame) {
        if (!body) return;
        const auto* coordinateParent = body->getCoordinateParent();
        const CFrame localFrame = coordinateParent
            ? coordinateParent->getWorldCFrame().inverse() * worldFrame
            : worldFrame;
        body->setCFrame(localFrame);
    };
    for (const auto& pending : pendingBodyPoses) {
        setBodyWorldPose(pending.body, pending.worldFrame);
    }
    if (pendingBodyPoses.empty()) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": no saved Ragdoll bind pose bodies were available"
        );
    }

    for (const auto& pending : pendingBodyPoses) {
        if (!physics->hasBody(*pending.body)) {
            RCBN_ERROR(
                "Humanoid \"" << getFullPath()
                << "\": bind pose body has no physics body \""
                << pending.body->getFullPath() << '"'
            );
            continue;
        }
        physics->setLinearVelocity(*pending.body, Vector3());
        physics->setAngularVelocity(*pending.body, Vector3());
    }
    m_recoveryFinalNormalizationApplied = true;
    logRagdollRecoveryRigState(physics, "after-normalization");

    // Re-register the normal physical bind constraints only after every body
    // has been moved and its velocity has been cleared.
    setMotor6DConstraintsEnabled(true);

    if (m_savedRootLockFlagsValid) {
        root->setLockFlags(m_savedRootLockFlags);
    }
    restoreRagdollCollision();

    if (auto gyro = m_rootGyro.lock()) {
        if (m_savedRootGyroYStateValid) {
            gyro->setTargetAngle(GyroAxis::Y, m_savedRootGyroYTarget);
        }
        gyro->setAxisEnabled(GyroAxis::Y, true);
        gyro->setAxisEnabled(GyroAxis::X, true);
        gyro->setTargetAngle(GyroAxis::X, 0.0f);
        gyro->setAxisEnabled(GyroAxis::Z, true);
        gyro->setTargetAngle(GyroAxis::Z, 0.0f);
        gyro->setEnabled(true);
    }
    if (auto yawForce = findCharacterYawForce(root)) {
        yawForce->Value = Vector3();
        yawForce->Enabled = false;
    }

    m_state = State::Normal;
    m_ragdollStableTime = 0.0f;
    m_recoveryElapsedTime = 0.0f;
    m_recoveryGroundedTime = 0.0f;
    m_recoveryUprightStableTime = 0.0f;
    m_recoveryFallbackStableTime = 0.0f;
    m_lastImpactStrength = 0.0f;
    m_savedRootGyroYStateValid = false;
    m_recoveryYawValid = false;
    m_hoverSuppressedForJump = false;
    isGrounded = false;
    updateGroundHover(physics, root);
    m_recoveryDiagnosticPendingPhysicsLog = true;

    // RCBN_LOG(
    //     "[Ragdoll] final normalization humanoid=\"" << getFullPath()
    //     << "\""
    //     << " mode=" << (recoveryReason ? recoveryReason : "unknown")
    //     << " yaw=" << finalYawDegrees
    //     << " supportY=" << finalSupportY
    //     << " currentY=" << currentY
    //     << " targetY=" << targetY
    //     << " yCorrection=" << (targetY - currentY)
    //     << " position=" << finalPosition.toString()
    // );
    // RCBN_LOG("[Ragdoll] state=Normal humanoid=\"" << getFullPath() << '"');
}

void Humanoid::updatePhysicsState(Physics* physics) {
    if (m_state == State::Ragdoll || m_state == State::Recovering) {
        setHoverForces(physics, false, 0.0f);
        setClimbForces(physics, false, Vector3());
        isGrounded = false;
        return;
    }
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root)) {
        setHoverForces(physics, false, 0.0f);
        setClimbForces(physics, false, Vector3());
        return;
    }

    const bool touchingTruss =
        physics->findOverlapping(*root, "Truss", 0.5f) != nullptr;
    if (m_trussControlSuppressed && !touchingTruss) {
        m_trussControlSuppressed = false;
    }
    const bool onTruss = touchingTruss && !m_trussControlSuppressed;
    if (onTruss) {
        setCharacterGravity(physics, false);
        setHoverForces(physics, false, 0.0f);
        isGrounded = false;
        return;
    }

    if (isClimbing()) {
        m_state = State::Normal;
    }
    setCharacterGravity(physics, true);
    setClimbForces(physics, false, Vector3());
    updateGroundHover(physics, root);
}

void Humanoid::stopCharacterMotion(Physics* physics) {
    if (!physics) {
        return;
    }

    auto root = getRootPart();
    if (auto yawForce = findCharacterYawForce(root)) {
        yawForce->Value = Vector3();
        yawForce->Enabled = false;
    }

    for (const auto& body : collectCharacterBodies()) {
        if (!body || !physics->hasBody(*body)) {
            continue;
        }

        const Vector3 velocity = physics->getLinearVelocity(*body);
        physics->setLinearVelocity(*body, Vector3(0.0f, velocity.y, 0.0f));
        physics->setAngularVelocity(*body, Vector3());
    }
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

    if (!isRagdoll()) enterRagdoll(physics);
    m_deathElapsed += dt;
}

bool Humanoid::isRespawnReady() const {
    return m_dead && m_deathElapsed >= RespawnTime;
}

void Humanoid::enterRagdoll(Physics* physics, float impactStrength) {
    if (isRagdoll()) return;

    auto root = getRootPart();
    if (!root) {
        if (auto parent = Parent.lock()) resolveParts(parent.get());
        root = getRootPart();
    }
    if (!root) {
        RCBN_ERROR(
            "Humanoid \"" << getFullPath()
            << "\": cannot enter Ragdoll without Root"
        );
        return;
    }

    saveRagdollBindPose();
    m_state = State::Ragdoll;
    m_trussControlSuppressed = false;
    m_ragdollStableTime = 0.0f;
    m_recoveryUprightStableTime = 0.0f;
    m_lastImpactStrength = impactStrength;
    m_savedRootLockFlags = root->LockFlags;
    m_savedRootLockFlagsValid = true;
    float entryYaw = 0.0f;
    if (tryGetHorizontalYaw(root->getWorldCFrame().Rotation, entryYaw)) {
        m_lastValidYawDegrees = entryYaw;
        m_hasLastValidYaw = true;
    }

    // RCBN_LOG(
    //     "[Ragdoll] entered humanoid=\"" << getFullPath()
    //     << "\" impact=" << impactStrength
    //     << " threshold=" << ImpactRagdollThreshold
    // );

    setHoverForces(physics, false, 0.0f);
    stopAnimation();

    if (auto gyro = m_rootGyro.lock()) {
        gyro->setEnabled(false);
    }

    if (auto yawForce = findCharacterYawForce(root)) {
        yawForce->Value.y = 0.0f;
        yawForce->Enabled = false;
    }

    root->setLockFlags(PhysicsLockFlags::None);
    // Save collision modes before changing any mode.  Root is included so
    // custom rigs restore exactly.
    m_savedCollisionModes.clear();
    for (const auto& body : collectCharacterBodies()) {
        if (!body) continue;
        m_savedCollisionModes.emplace_back(body, body->CanCollide);
    }
    for (const auto& body : collectCharacterBodies()) {
        if (!body) continue;
        body->setCanCollide(true);
        if (physics && physics->hasBody(*body))
            physics->setGravityEnabled(*body, true);
    }
    setRagdollConstraintsEnabled(true);
}

void Humanoid::recoverFromRagdoll(Physics* physics) {
    if (m_state != State::Ragdoll || m_dead) return;
    beginRagdollRecovery(physics);
}

void Humanoid::setRagdollStateForReplication(
    bool ragdoll,
    Physics* physics) {
    if (ragdoll) {
        if (!isRagdoll()) enterRagdoll(physics);
        return;
    }
    if (m_state == State::Ragdoll && !m_dead) {
        recoverFromRagdoll(physics);
    }
}

void Humanoid::setRagdollStateForReplication(bool ragdoll) {
    setRagdollStateForReplication(ragdoll, nullptr);

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

    if (auto root = getRootPart()) {
        float yawDegrees = 0.0f;
        if (tryGetHorizontalYaw(root->getWorldCFrame().Rotation, yawDegrees)) {
            m_lastValidYawDegrees = yawDegrees;
            m_hasLastValidYaw = true;
        }
    }

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
    if (m_dead || (m_state != State::Normal && !isClimbing())) {
        setHoverForces(physics, false, 0.0f);
        return;
    }
    auto root = getRootPart();
    if (!root || !physics || !physics->hasBody(*root)) {
        setHoverForces(physics, false, 0.0f);
        return;
    }

    float currentYawDegrees = 0.0f;
    if (tryGetHorizontalYaw(root->getWorldCFrame().Rotation, currentYawDegrees)) {
        m_lastValidYawDegrees = currentYawDegrees;
        m_hasLastValidYaw = true;
    }

    auto yawForce =
        findCharacterYawForce(root);

    // RCBN_LOG(
    //     "Humanoid YawForce rootPtr=" << static_cast<const void*>(root.get())
    //     << " rootPath=" << root->getFullPath()
    //     << " yawForcePtr=" << static_cast<const void*>(yawForce.get())
    // );

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
            rightArmRaised,
            deltaTime
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

    const Vector3 flatHeading(
        flatForward.x,
        0.0f,
        flatForward.z
    );
    if (flatHeading.lengthSquared() > 1.0e-8f) {
        const Vector3 targetHeading = flatHeading.normalize();
        if (m_smoothedHeadingDirection.lengthSquared() <= 1.0e-8f) {
            m_smoothedHeadingDirection = targetHeading;
        }
        else {
            m_smoothedHeadingDirection =
                m_smoothedHeadingDirection +
                (targetHeading - m_smoothedHeadingDirection) * smoothingAlpha;
            if (m_smoothedHeadingDirection.lengthSquared() > 1.0e-8f)
                m_smoothedHeadingDirection = m_smoothedHeadingDirection.normalize();
        }
    }

    // --- Truss(はしご)接触判定。登坂中は重力を切り、静止していても留まれるようにする ---
    BaseCube* trussCube =
        physics ? physics->findOverlapping(*root, "Truss", 0.5f) : nullptr;
    if (m_trussControlSuppressed) {
        if (trussCube) {
            trussCube = nullptr;
        } else {
            m_trussControlSuppressed = false;
        }
    }

    if (trussCube && forwardAxis < 0.0f) {
        float yawDegrees = 0.0f;
        if (!tryGetHorizontalYaw(root->getWorldCFrame().Rotation, yawDegrees)) {
            yawDegrees = m_hasLastValidYaw ? m_lastValidYawDegrees : 0.0f;
        }

        float supportY = 0.0f;
        if (findRootSupport(physics, yawDegrees, supportY)) {
            const float desiredRootHeight =
                std::isfinite(HipHeight) && HipHeight >= 0.0f
                    ? HipHeight
                    : std::max(root->Size.y * 0.5f, 0.1f);
            const float supportDistance =
                root->getWorldPosition().y - supportY;
            if (std::isfinite(supportDistance) &&
                supportDistance <=
                    desiredRootHeight + TRUSS_DESCENT_EXIT_MARGIN) {
                m_state = State::Normal;
                m_trussControlSuppressed = true;
                cancelCharacterDescent(physics);
                trussCube = nullptr;
            }
        }
    }

    if (trussCube) {
        if (forwardAxis > 0.0f) {
            m_state = State::ClimbingUp;
        } else if (forwardAxis < 0.0f) {
            m_state = State::ClimbingDown;
        } else if (isClimbing()) {
            m_state = State::Normal;
        }
        setCharacterGravity(physics, false);
        setHoverForces(physics, false, 0.0f);
    } else {
        if (isClimbing()) {
            m_state = State::Normal;
        }
        setCharacterGravity(physics, true);
        setClimbForces(physics, false, Vector3());
    }

    // --- 向き(Rotation)の更新 ---
    // Truss接触中は向きを固定する(自動回転させると登坂中に姿勢が崩れて落下してしまうため)
    if (!trussCube) {
        const Vector3* headingDirection = nullptr;

        if (ctrlLockEnabled) {
            headingDirection = &m_smoothedHeadingDirection;
        }
        else if (isPressingMove) {
            headingDirection = &currentMoveDir;
        }

        // An authored Angular Force owns yaw.  The generated RootGyro remains
        // the fallback for rigs without that controller and always stabilizes
        // pitch and roll.
        if (auto gyro = m_rootGyro.lock()) {
            gyro->setEnabled(true);
            gyro->setAxisEnabled(GyroAxis::Y, !yawForce);
            if (!yawForce && headingDirection &&
                headingDirection->lengthSquared() > 1e-8f) {
                gyro->setCharacterHeading(*headingDirection);
            }
        }

        if (yawForce) {
            if (headingDirection &&
                headingDirection->lengthSquared() > 1e-8f) {
                constexpr float DEGREES_TO_RADIANS =
                    0.01745329251994329577f;
                constexpr float MAX_TURN_SPEED = 8.0f;
                constexpr float MIN_CONTROL_HORIZON = 1.0f / 60.0f;
                constexpr float YAW_DEAD_ZONE_DEGREES = 0.5f;

                const float targetYaw =
                    Gyro::headingAngleFromDirection(*headingDirection);
                const float currentYaw = Gyro::angleFromRotation(
                    GyroAxis::Y,
                    root->getWorldCFrame().Rotation);
                float errorDegrees = targetYaw - currentYaw;
                while (errorDegrees > 180.0f) {
                    errorDegrees -= 360.0f;
                }
                while (errorDegrees < -180.0f) {
                    errorDegrees += 360.0f;
                }

                float desiredYawVelocity = 0.0f;
                if (std::abs(errorDegrees) > YAW_DEAD_ZONE_DEGREES) {
                    desiredYawVelocity = std::clamp(
                        errorDegrees * DEGREES_TO_RADIANS /
                            std::max(deltaTime, MIN_CONTROL_HORIZON),
                        -MAX_TURN_SPEED,
                        MAX_TURN_SPEED);
                }
                yawForce->Value.y = desiredYawVelocity;
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
        setClimbForces(physics, true, climbVel);
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

    applyBodyAnimation(leftArmRaised, rightArmRaised, deltaTime);
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
        updatePhysicsState(physics);
        return true;
    }

    Vector3 dir = toTarget.normalize();
    move(dir, Vector3::Cross(Vector3(0, 1, 0), dir), true, dir, false,
         physics, false, false, 0.0f, 0.0f, 0.15f, deltaTime);
    updatePhysicsState(physics);
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
    const bool climbingJump = isClimbing();
    if (m_dead || (m_state != State::Normal && !climbingJump) || !physics) {
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

    if (!climbingJump && !isGrounded && !submerged) {
        return;
    }

    if (climbingJump) {
        m_state = State::Normal;
        m_trussControlSuppressed = true;
        setCharacterGravity(physics, true);
        setClimbForces(physics, false, Vector3());
    }
    isGrounded = false;
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

    if ((m_state != State::Normal && !isClimbing()) ||
        !m_animPlaying || !m_currentAnim) {
        return;
    }

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
    if (!bodyPoseUpdated) applyBodyAnimation(false, false, dt);

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
        p.leftArm  = m_jumpShoulderAngle;
        p.rightArm = m_jumpShoulderAngle;
        p.leftLeg  = -swing;
        p.rightLeg =  swing;
    }
    return p;
}

// ============================================================
// Animation: Limb組み立て（共通）
// ============================================================

void Humanoid::applyBodyAnimation(bool leftArmRaised, bool rightArmRaised,
                                  float deltaTime) {
    m_bodyPoseUpdatedThisFrame = true; // 呼ばれた事実を記録(Root未解決で以降no-opでも「試行済み」として扱う)
    if (m_state != State::Normal && !isClimbing()) return;
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

    constexpr float JUMP_SHOULDER_MAX_ANGLE = 180.0f;
    constexpr float JUMP_SHOULDER_SPEED = 1200.0f;
    const float animationDeltaTime =
        std::isfinite(deltaTime) ? std::max(deltaTime, 0.0f) : 0.0f;

    // Keep the jump shoulder rotation as a continuous animation-side scalar.
    // The sign of this update is selected by the grounded state, never by a
    // quaternion shortest-path calculation.
    if (!isGrounded) {
        m_jumpShoulderAngle = std::min(
            JUMP_SHOULDER_MAX_ANGLE,
            m_jumpShoulderAngle + JUMP_SHOULDER_SPEED * animationDeltaTime
        );
    } else if (m_jumpShoulderAngle > 0.0f) {
        m_jumpShoulderAngle = std::max(
            0.0f,
            m_jumpShoulderAngle - JUMP_SHOULDER_SPEED * animationDeltaTime
        );
    }

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
    } else if (!isGrounded || m_jumpShoulderAngle > 0.0f) {
        // CFrame/Quaternion is only the final representation of the scalar
        // animation state.  Both shoulders intentionally use the same angle.
        const CFrame jumpShoulderDelta = CFrame::fromAxisAngle(
            Vector3(1, 0, 0),
            m_jumpShoulderAngle
        );
        leftArmDelta = jumpShoulderDelta;
        rightArmDelta = jumpShoulderDelta;
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
    if (g_humanoidUpdateAllDepth == 0) {
        ++g_humanoidUpdateAllInvocation;
        g_currentHumanoidUpdateAllInvocation = g_humanoidUpdateAllInvocation;
    }
    ++g_humanoidUpdateAllDepth;
    if (root->IsA("Humanoid")) {
        auto* humanoid = static_cast<Humanoid*>(root);
        humanoid->updateDeath(dt, physics);
        humanoid->updateRagdoll(dt, physics);
        humanoid->updateAnimation(dt);
    }
    for (auto const& [name, child] : root->getChildren())
        updateAll(child.get(), dt, physics);
    --g_humanoidUpdateAllDepth;
}
