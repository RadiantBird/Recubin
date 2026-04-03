#include "include/Instances/BaseCube.hpp"

BaseCube::BaseCube(Vector3 Pos, Vector3 Sz) 
    : Instance("BaseCube"), Position(Pos), Size(Sz), Color(1, 1, 1, 1) {
}

bool BaseCube::IsA(std::string className) {
    if (className == "BaseCube") {
        return true;
    }
    return Instance::IsA(className);
}

void BaseCube::syncPhysics() {
    if (!actor || Anchored) return;

    // PhysXから現在の姿勢を取得
    physx::PxTransform pose = actor->getGlobalPose();

    // 位置と回転（w, x, y, z）を同期
    this->Position = Vector3(pose.p.x, pose.p.y, pose.p.z);
    this->Rotation = Quaternion(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
}