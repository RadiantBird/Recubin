#pragma once
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>

class Physics {
private:
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxScene* scene = nullptr;
    physx::PxMaterial* defaultMaterial = nullptr;

    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;

public:
    void init();
    void update(Workspace& workspace, float dt);
    void createActor(BaseCube* cube);
};