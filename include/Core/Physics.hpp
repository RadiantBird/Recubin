#pragma once
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>
#include <vector>

class Physics {
private:
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxScene* scene = nullptr;
    physx::PxMaterial* defaultMaterial = nullptr;

    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;

    std::vector<BaseCube*> cubes; // 物理シミュレーション対象のキューブを管理

public:
    void init();
    void update(Workspace& workspace, float dt);
    void createActor(BaseCube* cube);
    void removeCube(BaseCube* cube);
};