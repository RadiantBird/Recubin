#include <Core/CharacterRig.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Sphere.hpp>
#include <Math/Quaternion.hpp>
#include <Util/Color4.hpp>
#include <PhysX/PxPhysicsAPI.h>

namespace CharacterRig {

void buildDefaultRigParts(const std::shared_ptr<Instance>& parent) {
    if (!parent) return;

    auto humanoid = std::make_shared<Humanoid>();
    humanoid->Name = "Humanoid";

    Vector3 basePos(0.0f, 0.0f, 0.0f);
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

    root->LockFlags = physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
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
}

} // namespace CharacterRig
