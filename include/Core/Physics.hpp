#pragma once
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Util/Material.hpp>
#include <include/Math/Quaternion.hpp>
#include <vector>
#include <unordered_map>

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Vector3 position;
    Vector3 normal;
    Instance* instance = nullptr;
};

class Physics {
private:
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxScene* scene = nullptr;
    std::unordered_map<MaterialType, physx::PxMaterial*> materialCache;

    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;

    std::vector<BaseCube*> cubes; // 物理シミュレーション対象のキューブを管理
    physx::PxMaterial* getOrCreateMaterial(const Material& m);

    struct PendingOp {
        enum class Type { Resize, SetRotation };
        Type      type;
        BaseCube* cube;
        Quaternion rotation; // SetRotation 時のみ使用
    };
    std::vector<PendingOp> m_pendingOps;

public:
    void init();
    virtual ~Physics();
    void update(Workspace& workspace, float dt);
    void createActor(BaseCube* cube);
    void recreateActor(BaseCube* cube);
    void removeCube(BaseCube* cube);

    void clearCubes() { cubes.clear(); }

    void enqueueResize(BaseCube* cube);
    void enqueueSetRotation(BaseCube* cube, Quaternion rot);

    bool raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, physx::PxRigidActor* ignoreActor = nullptr);
};