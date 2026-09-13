#include <Core/CharacterRig.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Animation.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/Gyro.hpp>
#include <Instances/Motor6D.hpp>
#include <Math/Quaternion.hpp>
#include <Util/Color4.hpp>
#include <iterator>
#include <utility>

namespace CharacterRig {

const CharacterGroundHeightSettings& groundHeightSettings() {
    // @RadiantBird 2026/09/13:
    // Root center and the unchanged bind pose put the feet bottom at y=-2.
    // Detection/capture remain close so hover cannot attract a distant fall.
    static const CharacterGroundHeightSettings settings{
        2.0f,
        3.0f,
        3.0f,
        120.0f,
        20.0f,
        1200.0f,
        1.0f,
    };
    return settings;
}

std::vector<std::shared_ptr<BaseCube>> collectR6Bodies(Instance* model) {
    std::vector<std::shared_ptr<BaseCube>> bodies;
    if (!model) return bodies;
    static constexpr const char* BODY_NAMES[] = {
        "Root", "Torso", "Head", "LeftArm", "RightArm",
        "LeftLeg", "RightLeg",
    };
    bodies.reserve(std::size(BODY_NAMES));
    const auto& children = model->getChildren();
    for (const char* name : BODY_NAMES) {
        const auto found = children.find(name);
        if (found == children.end()) continue;
        if (auto body = std::dynamic_pointer_cast<BaseCube>(found->second)) {
            bodies.push_back(std::move(body));
        }
    }
    return bodies;
}

const std::vector<R6JointBinding>& r6JointBindings() {
    static const std::vector<R6JointBinding> bindings = {
        {"Torso", "Torso", CFrame(0, 1, 0), CFrame()},
        {"Head", "Head", CFrame(0, 2.5f, 0), CFrame()},
        {"LeftShoulder", "LeftArm", CFrame(Vector3(-1.5f, 2, 0), Quaternion()) * CFrame(Vector3(0,-.5f,0)), CFrame(Vector3(0,.5f,0)) * CFrame(Vector3(0,-1,0))},
        {"RightShoulder", "RightArm", CFrame(Vector3(1.5f, 2, 0), Quaternion()) * CFrame(Vector3(0,-.5f,0)), CFrame(Vector3(0,.5f,0)) * CFrame(Vector3(0,-1,0))},
        {"LeftHip", "LeftLeg", CFrame(-.5f, 0, 0), CFrame(0,-1,0)},
        {"RightHip", "RightLeg", CFrame(.5f, 0, 0), CFrame(0,-1,0)}
    };
    return bindings;
}
const R6JointBinding* findR6Joint(const std::string& name) {
    for (const auto& b : r6JointBindings()) if (b.jointName == name) return &b;
    return nullptr;
}
const std::vector<R6JointTopology>& r6JointTopology() {
    static const std::vector<R6JointTopology> topology = {
        {"RootJoint", "Root", "Torso"},
        {"Neck", "Torso", "Head"},
        {"LeftShoulder", "Torso", "LeftArm"},
        {"RightShoulder", "Torso", "RightArm"},
        {"LeftHip", "Torso", "LeftLeg"},
        {"RightHip", "Torso", "RightLeg"},
    };
    return topology;
}
const R6JointTopology* findR6JointTopology(const std::string& name) {
    for (const auto& joint : r6JointTopology())
        if (joint.jointName == name) return &joint;
    return nullptr;
}
CFrame applyR6Joint(const CFrame& root, const R6JointBinding& binding, const CFrame& delta) {
    return root * binding.rootToJoint * delta * binding.jointToPartBind;
}
CFrame applyMotor6D(const CFrame& part0, const CFrame& c0,
                    const CFrame& transform, const CFrame& c1) {
    return part0 * c0 * transform * c1.inverse();
}
void calculateMotor6DBind(const CFrame& part0, const CFrame& part1,
                          CFrame& c0, CFrame& c1) {
    c0 = part0.inverse() * part1;
    c1 = CFrame();
}

static CFrame r6MotorC1(const std::string& jointName) {
    // @RadiantBird 2026/09/13:
    // Motor6D must preserve the original R6 rotation pivots.
    // C1 is the joint frame in Part1 local coordinates.
    if (
        jointName == "LeftShoulder" ||
        jointName == "RightShoulder"
    ) {
        return CFrame(0.0f, 0.5f, 0.0f);
    }

    if (
        jointName == "LeftHip" ||
        jointName == "RightHip"
    ) {
        return CFrame(0.0f, 1.0f, 0.0f);
    }

    return CFrame();
}

void buildDefaultRigParts(const std::shared_ptr<Instance>& parent, const Vector3& basePos) {
    if (!parent) return;

    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "Humanoid";
    auto walkAnimation = std::make_shared<Animation>();
    walkAnimation->Name = "R6Walk";
    walkAnimation->ContentPath = "assets/anims/r6_walk.rcanim";
    walkAnimation->loadContent();

    auto root     = std::make_shared<Cube>(basePos, Vector3(2.0f, 2.0f, 1.0f), 0);
    auto head     = std::make_shared<Sphere>(basePos, Vector3(1.25f, 1.25f, 1.25f));
    auto torso    = std::make_shared<Cube>(basePos, Vector3(2.0f, 2.0f, 1.0f), 0);
    auto leftArm  = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    auto rightArm = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    auto leftLeg  = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);
    auto rightLeg = std::make_shared<Cube>(basePos, Vector3(1.0f, 2.0f, 1.0f), 0);

    // headを90度回転させて顔が前を向くようにする
    head->setRotation(Quaternion::fromAxisAngle(Vector3(0, 1, 0), 90.0f));

    root->Name     = "Root";
    head->Name     = "Head";
    torso->Name    = "Torso";
    leftArm->Name  = "LeftArm";
    rightArm->Name = "RightArm";
    leftLeg->Name  = "LeftLeg";
    rightLeg->Name = "RightLeg";

    root->Anchored = head->Anchored = torso->Anchored = leftArm->Anchored = rightArm->Anchored = leftLeg->Anchored = rightLeg->Anchored = false;
    root->CanCollide = true;
    head->CanCollide = torso->CanCollide = leftArm->CanCollide = rightArm->CanCollide = leftLeg->CanCollide = rightLeg->CanCollide = false;

    root->LockFlags = PhysicsLockFlags::None;
    root->Color = Color4(1.0f, 0.5f, 0.5f, 0.0f); // NOTE: physics root は非表示 (alpha=0)

    torso->Color    = Color4::FromRGB(100, 12, 32);
    Color4 skin     = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    head->Color     = skin;
    leftArm->Color  = skin;
    rightArm->Color = skin;
    Color4 pants    = Color4::FromRGB(0, 36, 81);
    leftLeg->Color  = pants;
    rightLeg->Color = pants;

    parent->addChild(humanoid);
    parent->addChild(root);
    parent->addChild(head);
    parent->addChild(torso);
    parent->addChild(leftArm);
    parent->addChild(rightArm);
    parent->addChild(leftLeg);
    parent->addChild(rightLeg);
    parent->addChild(walkAnimation);

    // Establish the visible bind pose before deriving Motor6D bind frames.
    for (const auto& binding : r6JointBindings()) {
        auto part = std::dynamic_pointer_cast<BaseCube>(parent->getChildren().at(binding.partName));
        part->setWorldCFrame(applyR6Joint(root->getWorldCFrame(), binding, CFrame()));
    }

    for (const auto& topology : r6JointTopology()) {
        auto part0 = std::dynamic_pointer_cast<BaseCube>(parent->getChildren().at(topology.part0Name));
        auto part1 = std::dynamic_pointer_cast<BaseCube>(parent->getChildren().at(topology.part1Name));
        auto motor = std::make_shared<Motor6D>();
        motor->Name = topology.jointName;
        motor->setPart0(part0);
        motor->setPart1(part1);
        const CFrame c1 =
            r6MotorC1(
                topology.jointName
            );

        const CFrame c0 =
            part0->getWorldCFrame().inverse() *
            part1->getWorldCFrame() *
            c1;

        motor->setC0(c0);
        motor->setC1(c1);
        motor->setTransform(CFrame());
        parent->addChild(motor);

        auto ragdoll = std::make_shared<BallSocket>(part0, part1);
        ragdoll->Name = topology.jointName + "Ragdoll";
        ragdoll->Enabled = false;
        parent->addChild(ragdoll);
    }

    auto gyro = std::make_shared<Gyro>();
    gyro->Name = "RootGyro";
    gyro->setPart(root);

    // @RadiantBird 2026/09/12:
    // Character rigs require X/Z stabilization while Y controls facing.
    // X and Z are enabled with a target of 0 to remain upright.
    // Y is enabled to control the facing direction.
    gyro->setAxisEnabled(GyroAxis::X, true);
    gyro->setTargetAngle(GyroAxis::X, 0.0f);

    gyro->setAxisEnabled(GyroAxis::Z, true);
    gyro->setTargetAngle(GyroAxis::Z, 0.0f);

    gyro->setAxisEnabled(GyroAxis::Y, true);
    gyro->setTargetAngle(
        GyroAxis::Y,
        root->getWorldCFrame().Rotation.toEuler().y
    );

    parent->addChild(gyro);

    // 参照は全ての兄弟がparentへ接続された後に設定し、
    // YAMLとclone remapで使えるparent相対パスも同時に確定させる。
    humanoid->setWalkAnimation(walkAnimation);

    humanoid->resolveParts(parent.get());
}

} // namespace CharacterRig
