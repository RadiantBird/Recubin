#pragma once
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>
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
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxScene* scene = nullptr;
    std::unordered_map<MaterialType, physx::PxMaterial*> materialCache;

    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;

    struct CubeEntry {
        std::weak_ptr<BaseCube> cube;
        physx::PxRigidActor* actor = nullptr;
    };
    std::vector<CubeEntry> cubes; // 物理シミュレーション対象のキューブを管理
    physx::PxMaterial* getOrCreateMaterial(const Material& m);

    struct PendingOp {
        enum class Type { Resize, SetRotation };
        Type      type;
        std::weak_ptr<BaseCube> cube;
        Quaternion rotation; // SetRotation 時のみ使用
    };
    std::vector<PendingOp> m_pendingOps;

    struct ConstraintEntry {
        std::weak_ptr<Instance> constraint;
        physx::PxJoint* joint = nullptr; // Weld は nullptr（compound 管理）
    };
    std::vector<ConstraintEntry> m_constraints;

    // Weld グループを 1 つの compound として再構築する内部ヘルパー
    void rebuildGroup(const std::vector<std::shared_ptr<BaseCube>>& assembly);

public:
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
};