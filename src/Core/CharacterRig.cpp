#include <Core/CharacterRig.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Animation.hpp>
#include <Math/Quaternion.hpp>
#include <Util/Color4.hpp>

namespace CharacterRig {

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
CFrame applyR6Joint(const CFrame& root, const R6JointBinding& binding, const CFrame& delta) {
    return root * binding.rootToJoint * delta * binding.jointToPartBind;
}

void buildDefaultRigParts(const std::shared_ptr<Instance>& parent, const Vector3& basePos) {
    if (!parent) return;

    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "Humanoid";
    auto walkAnimation = std::make_shared<Animation>();
    walkAnimation->Name = "R6Walk";
    walkAnimation->ContentPath = "assets/anims/r6_walk.rcanim";
    walkAnimation->loadContent();

    auto root     = std::make_shared<Cube>(basePos, Vector3(2.0f, 4.0f, 1.0f), 0);
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

    head->Anchored = torso->Anchored = leftArm->Anchored = rightArm->Anchored = leftLeg->Anchored = rightLeg->Anchored = true;
    head->CanCollide = torso->CanCollide = leftArm->CanCollide = rightArm->CanCollide = leftLeg->CanCollide = rightLeg->CanCollide = false;

    root->LockFlags = PhysicsLockFlags::AngularX | PhysicsLockFlags::AngularZ;
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

    // 参照は全ての兄弟がparentへ接続された後に設定し、
    // YAMLとclone remapで使えるparent相対パスも同時に確定させる。
    humanoid->setWalkAnimation(walkAnimation);

    humanoid->resolveParts(parent.get());
    humanoid->applyBodyAnimation(false, false);
}

} // namespace CharacterRig
