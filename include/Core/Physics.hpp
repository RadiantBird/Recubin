#pragma once
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>
#include <functional>
#include <include/Instances/Rope.hpp>
#include <include/Instances/Rod.hpp>
#include <include/Instances/Weld.hpp>
#include <include/Instances/Motor.hpp>
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
    static physx::PxFoundation*            s_foundation;
    static physx::PxPhysics*               s_pxPhysics;
    static physx::PxDefaultCpuDispatcher*  s_dispatcher;
    static physx::PxDefaultAllocator       s_allocator;
    static physx::PxDefaultErrorCallback   s_errorCallback;
    static int                             s_refCount;

    physx::PxScene* scene = nullptr;
    std::unordered_map<MaterialType, physx::PxMaterial*> materialCache;
    float m_accumulator = 0.0f;

    struct CubeEntry {
        std::weak_ptr<BaseCube> cube;
        physx::PxRigidActor* actor = nullptr;
    };
    std::vector<CubeEntry> cubes;
    physx::PxMaterial* getOrCreateMaterial(const Material& m);

    struct PendingOp {
        enum class Type { Resize, SetRotation };
        Type      type;
        std::weak_ptr<BaseCube> cube;
        Quaternion rotation;
    };
    std::vector<PendingOp> m_pendingOps;

    struct ConstraintEntry {
        std::weak_ptr<Instance> constraint;
        physx::PxJoint* joint = nullptr;
    };
    std::vector<ConstraintEntry> m_constraints;

    void rebuildGroup(const std::vector<std::shared_ptr<BaseCube>>& assembly);

    physx::PxSimulationEventCallback* m_contactCallback = nullptr;

public:
    static std::function<void(BaseCube*, BaseCube*)> s_contactCallback;

    void init();
    virtual ~Physics();
    void update(Workspace& workspace, float dt);
    void createActor(const std::shared_ptr<BaseCube>& cube);
    void recreateActor(const std::shared_ptr<BaseCube>& cube);
    void removeCube(const std::shared_ptr<BaseCube>& cube);

    void clearCubes();

    void enqueueResize(const std::shared_ptr<BaseCube>& cube);
    void enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rot);

    void createRope(const std::shared_ptr<Rope>& rope);
    void createRod(const std::shared_ptr<Rod>& rod);
    void createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace);
    void createMotor(const std::shared_ptr<Motor>& motor);
    void removeConstraint(const std::shared_ptr<Instance>& c);

    bool raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, physx::PxRigidActor* ignoreActor = nullptr);

    void setGravity(const Vector3& g);
    Vector3 getGravity() const;

    // ---- Terrain 用アクセサ ----
    // buildChunkPhysics() から呼ばれる。
    physx::PxScene*   getScene()   const { return scene; }
    static physx::PxPhysics* GetPhysics() { return s_pxPhysics; }
};