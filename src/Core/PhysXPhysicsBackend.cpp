#include "include/Core/PhysXPhysicsBackend.hpp"
#include "include/Core/Physics.hpp"
#include "include/Util/Logger.hpp"
#include <include/Instances/Spatial.hpp>
#include <include/PhysX/cooking/PxCooking.h>
#include <include/Math/Units.hpp>
#include <unordered_set>
#include <algorithm>
#include <queue>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <include/Instances/LiquidCube.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Instances/Force.hpp>
#include <include/Instances/BallSocket.hpp>
#include <include/Instances/NoCollision.hpp>

// ===================================================
//  static メンバ定義
// ===================================================
physx::PxFoundation*            PhysXPhysicsBackend::s_foundation  = nullptr;
physx::PxPhysics*               PhysXPhysicsBackend::s_pxPhysics   = nullptr;
physx::PxDefaultCpuDispatcher*  PhysXPhysicsBackend::s_dispatcher  = nullptr;
physx::PxDefaultAllocator       PhysXPhysicsBackend::s_allocator;
physx::PxDefaultErrorCallback   PhysXPhysicsBackend::s_errorCallback;
int                             PhysXPhysicsBackend::s_refCount    = 0;

static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));

namespace {
physx::PxRigidActor* getActor(PhysicsBodyHandle handle) {
    return reinterpret_cast<physx::PxRigidActor*>(static_cast<std::uintptr_t>(handle.value));
}

PhysicsBodyHandle makeBodyHandle(physx::PxRigidActor* actor) {
    return PhysicsBodyHandle{
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(actor))
    };
}

physx::PxRigidStatic* getTerrainActor(PhysicsTerrainHandle handle) {
    return reinterpret_cast<physx::PxRigidStatic*>(
        static_cast<std::uintptr_t>(handle.value));
}

PhysicsTerrainHandle makeTerrainHandle(physx::PxRigidStatic* actor) {
    return PhysicsTerrainHandle{
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(actor))
    };
}

void setActor(PhysicsBodyHandle& handle, physx::PxRigidActor* actor) {
    handle = makeBodyHandle(actor);
}

physx::PxTransform toPxTransform(const CFrame& value) {
    return physx::PxTransform(
        physx::PxVec3(value.Position.x, value.Position.y, value.Position.z),
        physx::PxQuat(value.Rotation.x, value.Rotation.y,
                      value.Rotation.z, value.Rotation.w));
}

CFrame fromPxTransform(const physx::PxTransform& value) {
    return CFrame(
        Vector3(value.p.x, value.p.y, value.p.z),
        Quaternion(value.q.w, value.q.x, value.q.y, value.q.z));
}

float cframeDifference(const CFrame& first, const CFrame& second) {
    const Vector3 translation = first.Position - second.Position;
    if (!std::isfinite(translation.x) || !std::isfinite(translation.y) ||
        !std::isfinite(translation.z))
        return -std::numeric_limits<float>::infinity();
    const Quaternion& a = first.Rotation;
    const Quaternion& b = second.Rotation;
    const float aLength = std::sqrt(a.w*a.w + a.x*a.x + a.y*a.y + a.z*a.z);
    const float bLength = std::sqrt(b.w*b.w + b.x*b.x + b.y*b.y + b.z*b.z);
    if (!std::isfinite(aLength) || !std::isfinite(bLength) ||
        aLength < 1.0e-6f || bLength < 1.0e-6f)
        return -std::numeric_limits<float>::infinity();
    const float dot = std::clamp(std::abs(
        (a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z) / (aLength * bLength)),
        0.0f, 1.0f);
    return translation.length() + 2.0f * std::acos(dot);
}

std::vector<physx::PxVec3> toPxVertices(const std::vector<Vector3>& vertices) {
    std::vector<physx::PxVec3> result;
    result.reserve(vertices.size());
    for (const Vector3& vertex : vertices) {
        result.emplace_back(vertex.x, vertex.y, vertex.z);
    }
    return result;
}
}

// ===================================================
//  衝突通知コールバック
// ===================================================
struct RCBNContactCallback : physx::PxSimulationEventCallback {
    std::vector<std::pair<const void*, const void*>>* pending = nullptr;

    explicit RCBNContactCallback(
        std::vector<std::pair<const void*, const void*>>* pendingContacts)
        : pending(pendingContacts) {}

    void onContact(const physx::PxContactPairHeader& header,
                   const physx::PxContactPair* pairs,
                   physx::PxU32 pairCount) override {
        if (header.flags & (physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0 |
                             physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
            return;
        if (!pending || !pairs) return;
        for (physx::PxU32 index = 0; index < pairCount; ++index) {
            const auto& pair = pairs[index];
            if (pair.flags & (physx::PxContactPairFlag::eREMOVED_SHAPE_0 |
                              physx::PxContactPairFlag::eREMOVED_SHAPE_1))
                continue;
            const void* first = pair.shapes[0]
                ? pair.shapes[0]->userData : nullptr;
            const void* second = pair.shapes[1]
                ? pair.shapes[1]->userData : nullptr;
            if (first && second) pending->emplace_back(first, second);
        }
    }
    void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
    void onWake(physx::PxActor**, physx::PxU32) override {}
    void onSleep(physx::PxActor**, physx::PxU32) override {}
    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
    void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}
};

// NoCollision の対象候補ビット。両シェイプに立っている場合のみフィルターコールバックに回す
static constexpr physx::PxU32 FILTER_WORD0_NOCOLLISION_CANDIDATE = 1u;

// MaintainVelocity は線速度と角速度をそれぞれ独立して維持する。
struct MaintainVelocityState {
    bool linear = false;
    bool angular = false;
};

static MaintainVelocityState getMaintainVelocityState(const BaseCube& cube) {
    MaintainVelocityState state;
    for (const auto& entry : cube.children) {
        const auto& child = entry.second;
        if (!child || !child->IsA("Force")) continue;
        const auto* force = static_cast<const Force*>(child.get());
        if (!force->Enabled || !force->MaintainVelocity) continue;
        if (force->Torque) state.angular = true;
        else               state.linear = true;
    }
    return state;
}

static physx::PxRigidDynamicLockFlags toPxLockFlags(PhysicsLockFlags flags) {
    physx::PxRigidDynamicLockFlags result = (physx::PxRigidDynamicLockFlags)0;
    if (hasPhysicsLockFlag(flags, PhysicsLockFlags::LinearX))
        result |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
    if (hasPhysicsLockFlag(flags, PhysicsLockFlags::LinearY))
        result |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
    if (hasPhysicsLockFlag(flags, PhysicsLockFlags::LinearZ))
        result |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
    if (hasPhysicsLockFlag(flags, PhysicsLockFlags::AngularX))
        result |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
    if (hasPhysicsLockFlag(flags, PhysicsLockFlags::AngularY))
        result |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
    if (hasPhysicsLockFlag(flags, PhysicsLockFlags::AngularZ))
        result |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    return result;
}

// Touched 通知を有効にするカスタムフィルターシェーダー
static physx::PxFilterFlags rcbnFilterShader(
    physx::PxFilterObjectAttributes, physx::PxFilterData filterData0,
    physx::PxFilterObjectAttributes, physx::PxFilterData filterData1,
    physx::PxPairFlags& pairFlags, const void*, physx::PxU32)
{
    pairFlags = physx::PxPairFlag::eSOLVE_CONTACT
              | physx::PxPairFlag::eDETECT_DISCRETE_CONTACT
              | physx::PxPairFlag::eDETECT_CCD_CONTACT
              | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND;

    // word1 はCharacterごとの実行時ID。同一の非zero IDは接触ペア自体を
    // 生成しないため、接触解決とTouchedの両方が無効になる。
    if (filterData0.word1 != 0 && filterData0.word1 == filterData1.word1)
        return physx::PxFilterFlag::eSUPPRESS;

    if ((filterData0.word0 & FILTER_WORD0_NOCOLLISION_CANDIDATE) &&
        (filterData1.word0 & FILTER_WORD0_NOCOLLISION_CANDIDATE))
        return physx::PxFilterFlag::eCALLBACK;

    return physx::PxFilterFlag::eDEFAULT;
}

// NoCollision ペアの衝突可否をランタイムに判定するフィルターコールバック。
// rcbnFilterShader が eCALLBACK を返したペアのみここに回ってくる。
class RCBNFilterCallback : public physx::PxSimulationFilterCallback {
public:
    // Physics が所有する正規化済みペア集合への参照。simulate 中は変更されないためロック不要
    const std::set<std::pair<const void*, const void*>>* pairs = nullptr;

    physx::PxFilterFlags pairFound(
        physx::PxU64, physx::PxFilterObjectAttributes, physx::PxFilterData, const physx::PxActor*, const physx::PxShape* s0,
        physx::PxFilterObjectAttributes, physx::PxFilterData, const physx::PxActor*, const physx::PxShape* s1,
        physx::PxPairFlags& pairFlags) override
    {
        const void* u0 = s0 ? s0->userData : nullptr;
        const void* u1 = s1 ? s1->userData : nullptr;

        if (pairs && u0 && u1) {
            auto normalized = (u0 < u1) ? std::make_pair(u0, u1) : std::make_pair(u1, u0);
            if (pairs->count(normalized))
                return physx::PxFilterFlag::eSUPPRESS; // 衝突ペアを生成しない（resetFiltering で再評価される）
        }

        pairFlags = physx::PxPairFlag::eSOLVE_CONTACT
                  | physx::PxPairFlag::eDETECT_DISCRETE_CONTACT
                  | physx::PxPairFlag::eDETECT_CCD_CONTACT
                  | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND;
        return physx::PxFilterFlag::eDEFAULT;
    }

    void pairLost(physx::PxU64, physx::PxFilterObjectAttributes, physx::PxFilterData,
                  physx::PxFilterObjectAttributes, physx::PxFilterData, bool) override {}

    bool statusChange(physx::PxU64&, physx::PxPairFlags&, physx::PxFilterFlags&) override {
        return false;
    }
};

PhysXPhysicsBackend::PhysXPhysicsBackend(Physics* facade)
    : m_facade(facade) {}

void PhysXPhysicsBackend::setGravity(const Vector3& g) {
    if (scene) scene->setGravity(physx::PxVec3(g.x, g.y, g.z));
}

Vector3 PhysXPhysicsBackend::getGravity() const {
    if (!scene) return Vector3(0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f);
    physx::PxVec3 g = scene->getGravity();
    return Vector3(g.x, g.y, g.z);
}

bool PhysXPhysicsBackend::hasBody(const BaseCube& cube) const {
    return cube.m_physicsOwner == m_facade &&
        getActor(cube.m_bodyHandle) != nullptr;
}

bool PhysXPhysicsBackend::sharesBody(const BaseCube& first, const BaseCube& second) const {
    return first.m_physicsOwner == m_facade &&
        second.m_physicsOwner == m_facade && first.m_bodyHandle &&
        first.m_bodyHandle == second.m_bodyHandle;
}

CFrame PhysXPhysicsBackend::getBodyWorldCFrame(const BaseCube& cube) const {
    if (cube.m_physicsOwner != m_facade) return CFrame();
    auto* actor = getActor(cube.m_bodyHandle);
    if (!actor) return CFrame();
    return fromPxTransform(actor->getGlobalPose());
}

void PhysXPhysicsBackend::setBodyWorldCFrame(BaseCube& cube, const CFrame& worldCFrame) {
    if (cube.m_physicsOwner != m_facade) return;
    auto* actor = getActor(cube.m_bodyHandle);
    if (!actor) return;
    actor->setGlobalPose(toPxTransform(worldCFrame));
}

Vector3 PhysXPhysicsBackend::getLinearVelocity(const BaseCube& cube) const {
    if (cube.m_physicsOwner != m_facade) return Vector3();
    auto* actor = getActor(cube.m_bodyHandle);
    auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
    if (!dynamic) return Vector3();
    const physx::PxVec3 velocity = dynamic->getLinearVelocity();
    return Vector3(velocity.x, velocity.y, velocity.z);
}

void PhysXPhysicsBackend::setLinearVelocity(BaseCube& cube, const Vector3& velocity) {
    if (cube.m_physicsOwner != m_facade) return;
    auto* actor = getActor(cube.m_bodyHandle);
    auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
    if (!dynamic) return;
    dynamic->setLinearVelocity(physx::PxVec3(velocity.x, velocity.y, velocity.z));
}

void PhysXPhysicsBackend::setAngularVelocity(BaseCube& cube, const Vector3& velocity) {
    if (cube.m_physicsOwner != m_facade) return;
    auto* actor = getActor(cube.m_bodyHandle);
    auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
    if (!dynamic) return;
    dynamic->setAngularVelocity(physx::PxVec3(velocity.x, velocity.y, velocity.z));
}

void PhysXPhysicsBackend::setGravityEnabled(BaseCube& cube, bool enabled) {
    if (cube.m_physicsOwner != m_facade) return;
    m_gravityEnabled[&cube] = enabled;
    auto* actor = getActor(cube.m_bodyHandle);
    auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
    if (!dynamic) return;
    dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !enabled);
}

void PhysXPhysicsBackend::applyLockFlags(BaseCube& cube) {
    if (cube.m_physicsOwner != m_facade) return;
    auto* actor = getActor(cube.m_bodyHandle);
    auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
    if (!dynamic || (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) return;
    dynamic->setRigidDynamicLockFlags(toPxLockFlags(cube.LockFlags));
}

void PhysXPhysicsBackend::syncCube(BaseCube& cube) {
    if (cube.m_physicsOwner != m_facade) return;
    auto* actor = getActor(cube.m_bodyHandle);
    if (!actor) return;
    if (cube.Anchored) {
        auto* kinematic = actor->is<physx::PxRigidDynamic>();
        if (kinematic && (kinematic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
            const CFrame world = cube.getWorldCFrame();
            const physx::PxTransform cubeWorldPose = toPxTransform(world);
            const physx::PxTransform compoundTarget =
                cubeWorldPose.transform(toPxTransform(cube.m_compoundLocalOffset).getInverse());
            if (cube.m_weldKinematic)
                kinematic->setGlobalPose(compoundTarget);
            else
                kinematic->setKinematicTarget(compoundTarget);
        }
        return;
    }

    const physx::PxTransform pose =
        actor->getGlobalPose().transform(toPxTransform(cube.m_compoundLocalOffset));
    cube.setWorldCFrame(fromPxTransform(pose));
}

bool PhysXPhysicsBackend::init() {
    // 最初のインスタンスのみ共有リソースを構築
    if (s_refCount == 0) {
        s_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, s_allocator, s_errorCallback);
        if (!s_foundation) {
            RCBN_LOG("[WARN] PxCreateFoundation failed. Physics disabled on this platform.");
        } else {
            // stud基準のスケール（1m=20stud）。PhysX公式の「cm単位ならlength=100」と同じ理屈
            physx::PxTolerancesScale scale(METER_TO_STUD, METER_TO_STUD * EARTH_GRAVITY_MPS2);
            s_pxPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *s_foundation, scale);
            if (!s_pxPhysics) {
                RCBN_LOG("[WARN] PxCreatePhysics failed. Physics disabled on this platform.");
            } else {
                s_dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
                PxInitExtensions(*s_pxPhysics, nullptr);
            }
        }
    }
    ++s_refCount;

    // PhysXが利用できない環境(Mac向けスタブビルド等)ではsceneを作らず物理無効のまま抜ける
    if (!s_pxPhysics) return false;

    physx::PxSceneDesc sceneDesc(s_pxPhysics->getTolerancesScale());
    sceneDesc.gravity               = physx::PxVec3(0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f);
    sceneDesc.cpuDispatcher         = s_dispatcher;
    sceneDesc.filterShader          = rcbnFilterShader;
    m_contactCallback               = new RCBNContactCallback(&m_pendingContacts);
    sceneDesc.simulationEventCallback = m_contactCallback;
    m_filterCallback                = new RCBNFilterCallback();
    m_filterCallback->pairs         = &m_noCollisionPairs;
    sceneDesc.filterCallback        = m_filterCallback;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
    scene = s_pxPhysics->createScene(sceneDesc);
    return scene != nullptr;
}

bool PhysXPhysicsBackend::isAvailable() const {
    return scene != nullptr;
}

PhysicsBackendType PhysXPhysicsBackend::getType() const {
    return PhysicsBackendType::PhysX;
}

PhysicsConstraintHandle PhysXPhysicsBackend::allocateConstraintHandle() {
    if (m_nextConstraintHandle == 0) ++m_nextConstraintHandle;
    return PhysicsConstraintHandle{m_nextConstraintHandle++};
}

PhysXPhysicsBackend::ConstraintEntry* PhysXPhysicsBackend::findConstraintEntry(
    PhysicsConstraintHandle handle) {
    if (!handle) return nullptr;
    auto it = std::find_if(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& entry) { return entry.handle == handle; });
    return it == m_constraints.end() ? nullptr : &*it;
}

void PhysXPhysicsBackend::clearConstraintHandle(Instance& constraint) {
    if (constraint.IsA("Rope")) {
        static_cast<Rope&>(constraint).m_constraintHandle = {};
    } else if (constraint.IsA("Rod")) {
        static_cast<Rod&>(constraint).m_constraintHandle = {};
    } else if (constraint.IsA("Weld")) {
        static_cast<Weld&>(constraint).m_constraintHandle = {};
    } else if (constraint.IsA("Motor")) {
        static_cast<Motor&>(constraint).m_constraintHandle = {};
    } else if (constraint.IsA("BallSocket")) {
        static_cast<BallSocket&>(constraint).m_constraintHandle = {};
    } else if (constraint.IsA("NoCollision")) {
        static_cast<NoCollision&>(constraint).m_constraintHandle = {};
    }
}

PhysXPhysicsBackend::~PhysXPhysicsBackend() {
    for (auto& entry : m_constraints) {
        if (auto constraint = entry.constraint.lock()) clearConstraintHandle(*constraint);
        if (entry.joint) {
            entry.joint->release();
            entry.joint = nullptr;
        }
    }
    m_constraints.clear();
    for (auto& entry : m_noCollisionEntries) {
        if (auto constraint = entry.inst.lock()) clearConstraintHandle(*constraint);
    }
    m_noCollisionEntries.clear();
    m_noCollisionPairs.clear();

    for (physx::PxRigidStatic* actor : m_terrainActors) {
        if (scene) scene->removeActor(*actor);
        actor->release();
    }
    m_terrainActors.clear();

    if (scene) {
        std::unordered_set<physx::PxRigidActor*> released;
        for (auto& entry : cubes) {
            if (entry.actor && released.find(entry.actor) == released.end()) {
                scene->removeActor(*entry.actor);
                entry.actor->release();
                released.insert(entry.actor);
            }
            if (auto c = entry.cube.lock()) {
                c->m_bodyHandle = {};
                c->m_compoundLocalOffset = CFrame();
                c->m_physicsOwner = nullptr;
            }
            entry.actor = nullptr;
        }
        cubes.clear();
        scene->release();
        scene = nullptr;
    } else {
        for (auto& entry : cubes) {
            if (auto c = entry.cube.lock()) {
                c->m_bodyHandle = {};
                c->m_compoundLocalOffset = CFrame();
                c->m_physicsOwner = nullptr;
            }
            entry.actor = nullptr;
        }
        cubes.clear();
    }
    delete m_contactCallback;
    m_contactCallback = nullptr;
    delete m_filterCallback;
    m_filterCallback = nullptr;

    // createMaterial()が返した所有参照をbackend寿命の終端で対称に解放する。
    // shape側の参照は上で全actorを解放済み。
    for (auto& [key, material] : materialCache) {
        (void)key;
        if (material) material->release();
    }
    materialCache.clear();

    // 最後のインスタンスが共有リソースを解放
    --s_refCount;
    if (s_refCount == 0) {
        PxCloseExtensions();
        if (s_dispatcher) { s_dispatcher->release(); s_dispatcher = nullptr; }
        if (s_pxPhysics)  { s_pxPhysics->release();  s_pxPhysics  = nullptr; }
        if (s_foundation) { s_foundation->release();  s_foundation = nullptr; }
    }
}

PhysicsTerrainHandle PhysXPhysicsBackend::createTerrain(
    const PhysicsTerrainDescriptor& descriptor) {
    if (!scene || !s_pxPhysics) return {};

    physx::PxRigidStatic* actor = s_pxPhysics->createRigidStatic(
        physx::PxTransform(
            physx::PxVec3(descriptor.origin.x, descriptor.origin.y, descriptor.origin.z)));
    if (!actor) return {};

    physx::PxMaterial* material = s_pxPhysics->createMaterial(
        descriptor.staticFriction,
        descriptor.dynamicFriction,
        descriptor.restitution);
    if (!material) {
        actor->release();
        return {};
    }

    const physx::PxCookingParams cookingParams(s_pxPhysics->getTolerancesScale());
    std::size_t shapeCount = 0;

    const bool triangleDataValid =
        descriptor.vertices.size() >= 3 &&
        descriptor.indices.size() >= 3 &&
        descriptor.indices.size() % 3 == 0 &&
        std::all_of(
            descriptor.indices.begin(),
            descriptor.indices.end(),
            [&descriptor](std::uint32_t index) {
                return index < descriptor.vertices.size();
            });
    if (triangleDataValid) {
        const std::vector<physx::PxVec3> vertices = toPxVertices(descriptor.vertices);
        physx::PxTriangleMeshDesc meshDescriptor;
        meshDescriptor.points.data = vertices.data();
        meshDescriptor.points.count = static_cast<physx::PxU32>(vertices.size());
        meshDescriptor.points.stride = sizeof(physx::PxVec3);
        meshDescriptor.triangles.data = descriptor.indices.data();
        meshDescriptor.triangles.count =
            static_cast<physx::PxU32>(descriptor.indices.size() / 3);
        meshDescriptor.triangles.stride = sizeof(std::uint32_t) * 3;

        physx::PxTriangleMesh* mesh = meshDescriptor.isValid()
            ? PxCreateTriangleMesh(cookingParams, meshDescriptor)
            : nullptr;
        if (mesh) {
            physx::PxTriangleMeshGeometry geometry(mesh);
            geometry.meshFlags = physx::PxMeshGeometryFlag::eDOUBLE_SIDED;
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor, geometry, *material);
            if (shape) {
                shape->userData = descriptor.userData;
                ++shapeCount;
            }
            mesh->release();
        }
    }

    for (const PhysicsTerrainHullDescriptor& hull : descriptor.hulls) {
        if (hull.vertices.size() < 4) continue;

        const std::vector<physx::PxVec3> vertices = toPxVertices(hull.vertices);
        physx::PxConvexMeshDesc hullDescriptor;
        hullDescriptor.points.data = vertices.data();
        hullDescriptor.points.count = static_cast<physx::PxU32>(vertices.size());
        hullDescriptor.points.stride = sizeof(physx::PxVec3);
        hullDescriptor.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

        physx::PxConvexMesh* mesh = hullDescriptor.isValid()
            ? PxCreateConvexMesh(cookingParams, hullDescriptor)
            : nullptr;
        if (!mesh) continue;

        physx::PxConvexMeshGeometry geometry(mesh);
        physx::PxShape* shape =
            physx::PxRigidActorExt::createExclusiveShape(*actor, geometry, *material);
        if (shape) {
            shape->setLocalPose(toPxTransform(hull.localFrame));
            shape->userData = descriptor.userData;
            ++shapeCount;
        }
        mesh->release();
    }

    material->release();
    if (shapeCount == 0) {
        actor->release();
        return {};
    }

    actor->userData = descriptor.userData;
    scene->addActor(*actor);
    m_terrainActors.insert(actor);
    return makeTerrainHandle(actor);
}

PhysicsTerrainHandle PhysXPhysicsBackend::replaceTerrain(
    PhysicsTerrainHandle oldHandle,
    const PhysicsTerrainDescriptor& descriptor) {
    const bool empty =
        descriptor.vertices.empty() &&
        descriptor.indices.empty() &&
        descriptor.hulls.empty();
    if (empty) {
        destroyTerrain(oldHandle);
        return {};
    }

    const PhysicsTerrainHandle newHandle = createTerrain(descriptor);
    if (!newHandle) return oldHandle;

    destroyTerrain(oldHandle);
    return newHandle;
}

void PhysXPhysicsBackend::destroyTerrain(PhysicsTerrainHandle handle) {
    physx::PxRigidStatic* actor = getTerrainActor(handle);
    if (!actor) return;

    const auto it = m_terrainActors.find(actor);
    if (it == m_terrainActors.end()) return;
    m_terrainActors.erase(it);

    if (scene) scene->removeActor(*actor);
    actor->release();
}

physx::PxRigidActor* PhysXPhysicsBackend::buildActor(
    const std::shared_ptr<BaseCube>& cube,
    const physx::PxTransform& transform) {
    if (!cube || !s_pxPhysics) return nullptr;
    physx::PxRigidActor* actor = nullptr;
    if (cube->Anchored) {
        physx::PxRigidDynamic* kin = s_pxPhysics->createRigidDynamic(transform);
        if (!kin) return nullptr;
        kin->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
        actor = kin;
    } else {
        physx::PxRigidDynamic* dynamicActor = s_pxPhysics->createRigidDynamic(transform);
        if (!dynamicActor) return nullptr;
        dynamicActor->setRigidDynamicLockFlags(toPxLockFlags(cube->LockFlags));
        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
        dynamicActor->setSolverIterationCounts(8, 2);
        actor = dynamicActor;
    }

    if (cube->CanCollide) {
        physx::PxMaterial* pxMat = getOrCreateMaterial(cube->material);
        if (!pxMat) {
            actor->release();
            return nullptr;
        }

        switch (cube->getPhysicsShape()) {
        case PhysicsShape::Box: {
            physx::PxBoxGeometry geom(cube->Size.x/2, cube->Size.y/2, cube->Size.z/2);
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
            if (shape) shape->userData = cube.get();
            break;
        }
        case PhysicsShape::Sphere: {
            physx::PxSphereGeometry geom(cube->Size.x / 2.f);
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
            if (shape) shape->userData = cube.get();
            break;
        }
        case PhysicsShape::ConvexMesh: {
            auto verts = toPxVertices(cube->getConvexVertices());
            physx::PxCookingParams cookParams(s_pxPhysics->getTolerancesScale());
            physx::PxConvexMeshDesc desc;
            desc.points.count  = static_cast<physx::PxU32>(verts.size());
            desc.points.stride = sizeof(physx::PxVec3);
            desc.points.data   = verts.data();
            desc.flags         = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eQUANTIZE_INPUT;
            physx::PxDefaultMemoryOutputStream buf;
            physx::PxConvexMeshCookingResult::Enum result;
            if (!PxCookConvexMesh(cookParams, desc, buf, &result)) {
                actor->release();
                return nullptr;
            }
            physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
            physx::PxConvexMesh* mesh = s_pxPhysics->createConvexMesh(input);
            if (!mesh) {
                actor->release();
                return nullptr;
            }
            physx::PxMeshScale scale(physx::PxVec3(cube->Size.x, cube->Size.y, cube->Size.z));
            physx::PxConvexMeshGeometry geom(mesh, scale);
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
            if (shape) shape->userData = cube.get();
            mesh->release();
            break;
        }
        if (actor->getNbShapes() == 0) {
            actor->release();
            return nullptr;
        }
        }
    }

    // 新規actorのためresetFilteringは不要。query filter dataは変更しない。
    for (physx::PxU32 i = 0; i < actor->getNbShapes(); i++) {
        physx::PxShape* shape = nullptr;
        actor->getShapes(&shape, 1, i);
        if (!shape || shape->userData != cube.get()) continue;
        physx::PxFilterData fd = shape->getSimulationFilterData();
        if (isInNoCollisionPair(cube.get()))
            fd.word0 |= FILTER_WORD0_NOCOLLISION_CANDIDATE;
        fd.word1 = cube->m_characterCollisionGroup;
        shape->setSimulationFilterData(fd);
    }

    if (!cube->Anchored) {
        // Weld compound (rebuildGroup) と同様に質量を体積比例にする。
        // これが無いと浮力(体積比例)に対して質量が定数のままになり、大きい物体ほど
        // 浮力/質量比が発散して吹き飛ぶ(applyBuoyancy参照)。cube->MassDensity はユーザーが
        // Properties パネルで調整できる密度（BaseCube::MassDensity）。0以下はPhysXがrejectするため下限を設ける。
        auto* dyn = static_cast<physx::PxRigidDynamic*>(actor);
        if (actor->getNbShapes() > 0 && !physx::PxRigidBodyExt::updateMassAndInertia(*dyn, std::max(cube->MassDensity, 0.01f))) {
            // 失敗時（縮退ジオメトリ等）は質量ゼロ/不正な慣性のアクターをシーンに残さないよう固定値で保険をかける
            RCBN_WARN("updateMassAndInertia failed for: " << cube->Name << " — falling back to mass=1.0");
            dyn->setMass(1.0f);
            dyn->setMassSpaceInertiaTensor(physx::PxVec3(1.0f, 1.0f, 1.0f));
        }
        if (actor->getNbShapes() == 0) {
            dyn->setMass(1.0f);
            dyn->setMassSpaceInertiaTensor(physx::PxVec3(1.0f, 1.0f, 1.0f));
        }
    }
    actor->userData = cube.get();
    return actor;
}

void PhysXPhysicsBackend::createActor(const std::shared_ptr<BaseCube>& cube) {
    if (!cube || !scene || getActor(cube->m_bodyHandle)) return;
    const CFrame world = cube->getWorldCFrame();
    const physx::PxTransform transform(
        physx::PxVec3(world.Position.x, world.Position.y, world.Position.z),
        physx::PxQuat(world.Rotation.x, world.Rotation.y,
                      world.Rotation.z, world.Rotation.w));
    physx::PxRigidActor* actor = buildActor(cube, transform);
    if (!actor) {
        RCBN_ERROR("PhysX body creation failed; logical Cube retained: "
                   << cube->Name);
        return;
    }
    scene->addActor(*actor);
    setActor(cube->m_bodyHandle, actor);
    cube->m_physicsOwner = m_facade;
    cube->m_weldKinematic = false;
    auto existing = std::find_if(
        cubes.begin(), cubes.end(),
        [&](const CubeEntry& entry) { return entry.cubeRaw == cube.get(); });
    if (existing == cubes.end())
        cubes.push_back({cube, cube.get(), actor});
    else {
        existing->cube = cube;
        existing->actor = actor;
    }
}

namespace {
class MemberRaycastFilter final : public physx::PxQueryFilterCallback {
public:
    const BaseCube* ignored = nullptr;

    physx::PxQueryHitType::Enum preFilter(
        const physx::PxFilterData&, const physx::PxShape* shape,
        const physx::PxRigidActor*, physx::PxHitFlags&) override {
        return ignored && shape && shape->userData == ignored
            ? physx::PxQueryHitType::eNONE
            : physx::PxQueryHitType::eBLOCK;
    }

    physx::PxQueryHitType::Enum postFilter(
        const physx::PxFilterData&, const physx::PxQueryHit&,
        const physx::PxShape*, const physx::PxRigidActor*) override {
        return physx::PxQueryHitType::eBLOCK;
    }
};
}

bool PhysXPhysicsBackend::raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, const BaseCube* ignoreCube) {
    if (!scene) return false;

    physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
    physx::PxVec3 pxDir(direction.x, direction.y, direction.z);

    if (pxDir.magnitudeSquared() < 1e-6f) return false;
    pxDir.normalize();

    // 複数のヒットを想定（自分自身を突き抜けるため）
    const physx::PxU32 maxHits = 16;
    physx::PxRaycastHit hitBuffer[maxHits];
    physx::PxRaycastBuffer buf(hitBuffer, maxHits);

    MemberRaycastFilter memberFilter;
    memberFilter.ignored = ignoreCube;
    physx::PxQueryFilterData filterData;
    filterData.flags |= physx::PxQueryFlag::ePREFILTER;
    bool status = scene->raycast(
        pxOrigin, pxDir, maxDistance, buf, physx::PxHitFlag::eDEFAULT,
        filterData, &memberFilter);

    if (status) {
        // ヒットしたアクターを走査し、無視対象以外を見つける
        physx::PxRaycastHit* bestHit = nullptr;

        // 通常のブロッキングヒットを確認
        if (buf.hasBlock) {
            if (!ignoreCube || !buf.block.shape ||
                buf.block.shape->userData != ignoreCube) {
                bestHit = &buf.block;
            }
        }

        // touchesはBVH走査順で距離順ではない（フィルタコールバック無しだと全ヒットが
        // touchesに入りhasBlockは立たない）ため、全走査して最近接を選ぶ
        for (physx::PxU32 i = 0; i < buf.nbTouches; i++) {
            if (ignoreCube && buf.touches[i].shape &&
                buf.touches[i].shape->userData == ignoreCube) continue;
            if (!bestHit || buf.touches[i].distance < bestHit->distance) {
                bestHit = &buf.touches[i];
            }
        }

        if (bestHit) {
            hitResult.hit = true;
            hitResult.distance = bestHit->distance;
            hitResult.position = Vector3(bestHit->position.x, bestHit->position.y, bestHit->position.z);
            hitResult.normal   = Vector3(bestHit->normal.x,   bestHit->normal.y,   bestHit->normal.z);
            if (bestHit->shape && bestHit->shape->userData) {
                hitResult.instance = static_cast<Instance*>(bestHit->shape->userData);
            } else {
                hitResult.instance = nullptr;
            }
            return true;
        }
    }

    hitResult.hit = false;
    return false;
}

physx::PxMaterial* PhysXPhysicsBackend::getOrCreateMaterial(const Material& m) {
    auto q = [](float v){ return (int)std::lround(v * 1000.0f); };
    MatKey key{ q(m.staticFriction), q(m.dynamicFriction), q(m.restitution) };
    auto it = materialCache.find(key);
    if (it != materialCache.end()) return it->second;

    physx::PxMaterial* pxMat = s_pxPhysics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
    if (!pxMat) return nullptr;
    materialCache[key] = pxMat;
    return pxMat;
}

void PhysXPhysicsBackend::enqueueResize(const std::shared_ptr<BaseCube>& cube) {
    m_pendingOps.push_back({ PendingOp::Type::Resize, std::weak_ptr<BaseCube>(cube), {} });
}

void PhysXPhysicsBackend::enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rot) {
    m_pendingOps.push_back({ PendingOp::Type::SetRotation, std::weak_ptr<BaseCube>(cube), rot });
}

void PhysXPhysicsBackend::recreateActor(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;

    physx::PxTransform savedPose(physx::PxIdentity);
    physx::PxVec3 savedLinearVelocity(0.0f);
    physx::PxVec3 savedAngularVelocity(0.0f);
    bool restorePose = false;
    bool restoreDynamicVelocity = false;
    auto* cubeActor = getActor(cube->m_bodyHandle);
    if (auto* dynamic = cubeActor ? cubeActor->is<physx::PxRigidDynamic>() : nullptr) {
        savedPose = dynamic->getGlobalPose();
        restorePose = true;
        if (!(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
            savedLinearVelocity = dynamic->getLinearVelocity();
            savedAngularVelocity = dynamic->getAngularVelocity();
            restoreDynamicVelocity = true;
        }
    }

    // Weld 等で他キューブと compound(共有アクター)を組んでいる場合、単純に
    // remove/release/createActor すると compound 全体を破棄してしまい、他メンバーの
    // cubeのbody handleが解放済みbodyを指したまま残ってしまう(暗黙的な溶接解除+UAFの原因)。
    // 共有中と判定できたら rebuildGroup() でグループ全体を再構築する。
    if (cubeActor) {
        std::vector<std::shared_ptr<BaseCube>> sharedGroup;
        for (auto& entry : cubes) {
            if (entry.actor == cubeActor) {
                if (auto c = entry.cube.lock()) sharedGroup.push_back(c);
            }
        }
        if (sharedGroup.size() > 1) {
            rebuildGroup(sharedGroup); // cubes/m_constraints(Weld/Rope/Rod/Motor)の同期も内部で完結
            return;
        }
    }

    if (!restorePose) {
        const CFrame world = cube->getWorldCFrame();
        savedPose = physx::PxTransform(
            physx::PxVec3(world.Position.x, world.Position.y, world.Position.z),
            physx::PxQuat(world.Rotation.x, world.Rotation.y,
                          world.Rotation.z, world.Rotation.w));
    }
    // scene未登録actorへshape・質量・lockを完成させる。失敗時は旧actorと
    // それに接続するjointを一切変更しない。
    physx::PxRigidActor* replacement = buildActor(cube, savedPose);
    if (!replacement) {
        RCBN_ERROR("PhysX body replacement failed; old body retained: "
                   << cube->Name);
        return;
    }
    if (restoreDynamicVelocity) {
        if (auto* dynamic = replacement->is<physx::PxRigidDynamic>();
            dynamic && !(dynamic->getRigidBodyFlags() &
                         physx::PxRigidBodyFlag::eKINEMATIC)) {
            dynamic->setLinearVelocity(savedLinearVelocity);
            dynamic->setAngularVelocity(savedAngularVelocity);
        }
    }

    if (cubeActor) {
        scene->removeActor(*cubeActor);
        cubeActor->release();
    }
    scene->addActor(*replacement);
    setActor(cube->m_bodyHandle, replacement);
    cube->m_physicsOwner = m_facade;
    cube->m_weldKinematic = false;
    cubeActor = replacement;

    // cubes 配列内の古いエントリを新アクターに同期（同期しないと cleanup ループが
    // 解放済みの古いポインタに対して二重に removeActor/release してしまう）
    for (auto& entry : cubes) {
        if (entry.cube.lock() == cube) {
            entry.actor = cubeActor;
            entry.cubeRaw = cube.get();
            break;
        }
    }

    // このcubeを参照する制約のジョイントを再構築（古いアクターへのダングリング防止）
    std::vector<std::shared_ptr<Instance>> toRebuild;
    for (auto& entry : m_constraints) {
        auto inst = entry.constraint.lock();
        if (!inst) continue;
        bool involves = false;
        if (inst->IsA("Rope")) {
            auto r = std::static_pointer_cast<Rope>(inst);
            involves = (r->m_cube0.lock() == cube || r->m_cube1.lock() == cube);
        } else if (inst->IsA("Rod")) {
            auto r = std::static_pointer_cast<Rod>(inst);
            involves = (r->m_cube0.lock() == cube || r->m_cube1.lock() == cube);
        } else if (inst->IsA("Motor")) {
            auto m = std::static_pointer_cast<Motor>(inst);
            involves = (m->m_cube0.lock() == cube || m->m_cube1.lock() == cube);
        } else if (inst->IsA("BallSocket")) {
            auto b = std::static_pointer_cast<BallSocket>(inst);
            involves = (b->m_cube0.lock() == cube || b->m_cube1.lock() == cube);
        }
        if (involves) {
            if (entry.joint) {
                entry.joint->release();
                entry.joint = nullptr;
            }
            clearConstraintHandle(*inst);
            toRebuild.push_back(inst);
        }
    }
    // push_back によるイテレータ無効化を避けるため削除してから再生成
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
            [&](const ConstraintEntry& e) {
                auto inst = e.constraint.lock();
                return inst && std::find(toRebuild.begin(), toRebuild.end(), inst) != toRebuild.end();
            }),
        m_constraints.end()
    );
    for (auto& inst : toRebuild) {
        if (inst->IsA("Rope")) {
            createRope(std::static_pointer_cast<Rope>(inst));
        } else if (inst->IsA("Rod")) {
            createRod(std::static_pointer_cast<Rod>(inst));
        } else if (inst->IsA("Motor")) {
            createMotor(std::static_pointer_cast<Motor>(inst));
        } else if (inst->IsA("BallSocket")) {
            createBallSocket(std::static_pointer_cast<BallSocket>(inst));
        }
    }
}

void PhysXPhysicsBackend::clearCubes() {
    m_pendingContacts.clear();
    // 1. ジョイントを解放（Weld の compound は cubes ループで解放）
    for (auto& entry : m_constraints) {
        if (auto constraint = entry.constraint.lock()) clearConstraintHandle(*constraint);
        if (entry.joint) {
            entry.joint->release();
            entry.joint = nullptr;
        }
    }
    m_constraints.clear();
    for (auto& entry : m_noCollisionEntries) {
        if (auto constraint = entry.inst.lock()) clearConstraintHandle(*constraint);
    }
    m_noCollisionEntries.clear();
    m_noCollisionPairs.clear();

    // 2. cube actor を release（compound は最初の参照で release し以降スキップ）
    std::unordered_set<physx::PxRigidActor*> released;
    for (auto& entry : cubes) {
        if (entry.actor && released.find(entry.actor) == released.end()) {
            if (scene) scene->removeActor(*entry.actor);
            entry.actor->release();
            released.insert(entry.actor);
        }
        if (auto c = entry.cube.lock()) {
            c->m_bodyHandle = {};
            c->m_compoundLocalOffset = CFrame();
            c->m_physicsOwner = nullptr;
        }
    }
    cubes.clear();
    m_gravityEnabled.clear();
    m_pendingOps.clear();
}

void PhysXPhysicsBackend::removeCube(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;
    m_gravityEnabled.erase(cube.get());

    physx::PxRigidActor* a = getActor(cube->m_bodyHandle);

    // actor を破棄する前に、移動する endpoint に結び付いた native
    // joint/filter を解除する。Weld は native joint を持たないため、論理
    // topology を残して復帰時に再接続できるようにする。
    std::vector<std::shared_ptr<Instance>> attached;
    for (const auto& entry : m_constraints) {
        auto value = entry.constraint.lock();
        if (!value || value->IsA("Weld")) continue;
        std::shared_ptr<BaseCube> first;
        std::shared_ptr<BaseCube> second;
        if (value->IsA("Rope")) {
            auto c = std::static_pointer_cast<Rope>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Rod")) {
            auto c = std::static_pointer_cast<Rod>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("Motor")) {
            auto c = std::static_pointer_cast<Motor>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        } else if (value->IsA("BallSocket")) {
            auto c = std::static_pointer_cast<BallSocket>(value);
            first = c->m_cube0.lock(); second = c->m_cube1.lock();
        }
        if (first == cube || second == cube) attached.push_back(value);
    }
    for (const auto& entry : m_noCollisionEntries) {
        auto value = entry.inst.lock();
        if (value && (entry.c0.lock() == cube || entry.c1.lock() == cube))
            attached.push_back(value);
    }
    for (const auto& value : attached) removeConstraint(value);

    cube->m_bodyHandle = {};
    cube->m_compoundLocalOffset = CFrame();
    cube->m_physicsOwner = nullptr;

    cubes.erase(std::remove_if(cubes.begin(), cubes.end(),
        [&](const CubeEntry& entry) {
            return entry.cubeRaw == cube.get() || entry.cube.lock() == cube;
        }), cubes.end());

    if (!a) return;
    std::vector<std::shared_ptr<BaseCube>> survivors;
    for (const auto& entry : cubes) {
        if (entry.actor != a) continue;
        if (auto other = entry.cube.lock()) survivors.push_back(other);
    }
    if (survivors.empty()) {
        if (scene) scene->removeActor(*a);
        a->release();
    } else {
        // rebuildGroup は旧 actor から全 member の姿勢と速度を保存し、
        // 残留 member の shape だけを持つ actor に差し替える。
        rebuildGroup(survivors);
    }
}

void PhysXPhysicsBackend::onCubeDestroyed(BaseCube& cube) {
    m_gravityEnabled.erase(&cube);
    physx::PxRigidActor* actor = getActor(cube.m_bodyHandle);

    if (actor) {
        if (actor->userData == &cube) actor->userData = nullptr;
        for (physx::PxU32 i = 0; i < actor->getNbShapes(); ++i) {
            physx::PxShape* shape = nullptr;
            actor->getShapes(&shape, 1, i);
            if (shape && shape->userData == &cube) shape->userData = nullptr;
        }
    }

    cube.m_bodyHandle = {};
    cube.m_compoundLocalOffset = CFrame();
    cube.m_physicsOwner = nullptr;

    cubes.erase(std::remove_if(cubes.begin(), cubes.end(),
        [&](const CubeEntry& entry) { return entry.cubeRaw == &cube; }),
        cubes.end());

    if (!actor) return;
    std::vector<std::shared_ptr<BaseCube>> survivors;
    for (const auto& entry : cubes) {
        if (entry.actor != actor) continue;
        if (auto other = entry.cube.lock()) survivors.push_back(other);
    }
    if (survivors.empty()) {
        if (scene) scene->removeActor(*actor);
        actor->release();
    } else {
        // shape userData を null にするだけでは衝突形状自体が残る。
        // 生存 member のみで compound を再構築し ghost shape を除去する。
        rebuildGroup(survivors);
    }
}

// 水中での減衰係数（大きいほど早く落ち着く。空中では 0 に戻す）
static constexpr float LIQUID_LINEAR_DAMPING  = 3.0f;
static constexpr float LIQUID_ANGULAR_DAMPING = 3.0f;

// サンプル位置にそのまま浮力を掛けると復元トルクが強くなりやすいため、
// 重心からの距離を縮めてトルクだけを穏やかにする（合計浮力は維持する）。
static constexpr float BUOYANCY_TORQUE_SCALE = 0.35f;

// 浮力を体積に分散させるためのグリッドサンプリング解像度（1軸あたりの点数）。
// サンプルは物体ローカル空間に置いてからワールドへ回転変換するため、物体側はOBBになる。
static constexpr int BUOYANCY_SAMPLE_RES = 5;

float PhysXPhysicsBackend::aabbOverlapVolume(const Vector3& posA, const Vector3& sizeA, const Vector3& posB, const Vector3& sizeB) {
    Vector3 aMin = posA - sizeA * 0.5f, aMax = posA + sizeA * 0.5f;
    Vector3 bMin = posB - sizeB * 0.5f, bMax = posB + sizeB * 0.5f;
    float ox = std::max(0.0f, std::min(aMax.x, bMax.x) - std::max(aMin.x, bMin.x));
    float oy = std::max(0.0f, std::min(aMax.y, bMax.y) - std::max(aMin.y, bMin.y));
    float oz = std::max(0.0f, std::min(aMax.z, bMax.z) - std::max(aMin.z, bMin.z));
    return ox * oy * oz;
}

BaseCube* PhysXPhysicsBackend::findOverlapping(const BaseCube& cube, const std::string& className, float margin) const {
    Vector3 cp = cube.getWorldPosition();
    Vector3 cs = cube.Size + Vector3(margin * 2.0f, margin * 2.0f, margin * 2.0f);
    for (auto& e : cubes) {
        auto other = e.cube.lock();
        if (!other || other.get() == &cube) continue;
        if (!other->IsA(className)) continue;
        if (aabbOverlapVolume(cp, cs, other->getWorldPosition(), other->Size) > 0.0f)
            return other.get();
    }
    return nullptr;
}

void PhysXPhysicsBackend::applyBuoyancy() {
    if (!scene) return;
    physx::PxVec3 g = scene->getGravity();
    Vector3 gVec(g.x, g.y, g.z);

    std::vector<BaseCube*> liquids;
    for (auto& e : cubes)
        if (auto c = e.cube.lock())
            if (c->IsA("LiquidCube")) liquids.push_back(c.get());

    std::unordered_set<physx::PxRigidDynamic*> visited;
    for (const auto& bodyEntry : cubes) {
        auto* dyn = bodyEntry.actor
            ? bodyEntry.actor->is<physx::PxRigidDynamic>() : nullptr;
        if (!dyn || !visited.insert(dyn).second ||
            (dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
            continue;

        std::vector<std::shared_ptr<BaseCube>> members;
        bool maintainLinear = false;
        bool maintainAngular = false;
        for (const auto& entry : cubes) {
            if (entry.actor != dyn) continue;
            auto member = entry.cube.lock();
            if (!member) continue;
            const MaintainVelocityState state = getMaintainVelocityState(*member);
            maintainLinear = maintainLinear || state.linear;
            maintainAngular = maintainAngular || state.angular;
            if (!member->IsA("LiquidCube") && member->CanCollide)
                members.push_back(member);
        }

        float totalBodyVolume = 0.0f;
        float totalSubmergedVolume = 0.0f;
        const CFrame bodyWorld = fromPxTransform(dyn->getGlobalPose());
        constexpr int GRID = BUOYANCY_SAMPLE_RES;
        constexpr int SAMPLE_COUNT = GRID * GRID * GRID;
        for (const auto& member : members) {
            const Vector3 size = member->Size;
            const float memberVolume = std::max(
                std::abs(size.x * size.y * size.z), 1e-4f);
            totalBodyVolume += memberVolume;
            const float sampleVolume =
                memberVolume / static_cast<float>(SAMPLE_COUNT);
            const CFrame memberWorld =
                bodyWorld * member->m_compoundLocalOffset;

            for (int ix = 0; ix < GRID; ++ix) {
                for (int iy = 0; iy < GRID; ++iy) {
                    for (int iz = 0; iz < GRID; ++iz) {
                        const Vector3 localPosition(
                            ((ix + 0.5f) / GRID - 0.5f) * size.x,
                            ((iy + 0.5f) / GRID - 0.5f) * size.y,
                            ((iz + 0.5f) / GRID - 0.5f) * size.z);
                        const Vector3 position =
                            memberWorld.pointToWorld(localPosition);
                        bool submerged = false;
                        float density = 0.0f;
                        for (BaseCube* liquid : liquids) {
                            const Vector3 minimum =
                                liquid->getWorldPosition() - liquid->Size * 0.5f;
                            const Vector3 maximum =
                                liquid->getWorldPosition() + liquid->Size * 0.5f;
                            if (position.x < minimum.x || position.x > maximum.x ||
                                position.y < minimum.y || position.y > maximum.y ||
                                position.z < minimum.z || position.z > maximum.z)
                                continue;
                            submerged = true;
                            density += std::max(
                                0.0f, static_cast<LiquidCube*>(liquid)->Density);
                        }
                        if (submerged) totalSubmergedVolume += sampleVolume;
                        if (maintainLinear || density <= 0.0f) continue;

                        const float buoyantVolume = sampleVolume * density;
                        const physx::PxVec3 force(
                            -gVec.x * buoyantVolume,
                            -gVec.y * buoyantVolume,
                            -gVec.z * buoyantVolume);
                        const Vector3 applicationPoint = memberWorld.Position +
                            (position - memberWorld.Position) *
                                BUOYANCY_TORQUE_SCALE;
                        physx::PxRigidBodyExt::addForceAtPos(
                            *dyn, force,
                            physx::PxVec3(applicationPoint.x,
                                          applicationPoint.y,
                                          applicationPoint.z),
                            physx::PxForceMode::eFORCE);
                    }
                }
            }
        }

        const float fraction = totalBodyVolume > 1e-4f
            ? std::clamp(totalSubmergedVolume / totalBodyVolume, 0.0f, 1.0f)
            : 0.0f;
        // LiquidCubeが0個の場合も毎fixed tick必ず0へ戻す。
        dyn->setLinearDamping(
            maintainLinear ? 0.0f : LIQUID_LINEAR_DAMPING * fraction);
        dyn->setAngularDamping(
            maintainAngular ? 0.0f : LIQUID_ANGULAR_DAMPING * fraction);
    }
}

void PhysXPhysicsBackend::applyForces() {
    std::unordered_set<physx::PxRigidDynamic*> visited;
    for (const auto& bodyEntry : cubes) {
        auto* dyn = bodyEntry.actor
            ? bodyEntry.actor->is<physx::PxRigidDynamic>() : nullptr;
        if (!dyn || !visited.insert(dyn).second ||
            (dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
            continue;

        bool maintainLinear = false;
        bool maintainAngular = false;
        bool gravityEnabled = true;
        Vector3 linearTarget;
        Vector3 angularTarget;
        std::vector<const Force*> additive;
        for (const auto& entry : cubes) {
            if (entry.actor != dyn) continue;
            auto member = entry.cube.lock();
            if (!member) continue;
            if (const auto gravity = m_gravityEnabled.find(member.get());
                gravity != m_gravityEnabled.end())
                gravityEnabled = gravityEnabled && gravity->second;
            for (const auto& [name, child] : member->children) {
                (void)name;
                if (!child || !child->IsA("Force")) continue;
                const auto* force = static_cast<const Force*>(child.get());
                if (!force->Enabled) continue;
                if (!force->MaintainVelocity) {
                    additive.push_back(force);
                } else if (force->Torque) {
                    maintainAngular = true;
                    angularTarget = force->Value;
                } else {
                    maintainLinear = true;
                    linearTarget = force->Value;
                }
            }
        }

        dyn->setActorFlag(
            physx::PxActorFlag::eDISABLE_GRAVITY,
            maintainLinear || !gravityEnabled);
        for (const Force* force : additive) {
            const physx::PxVec3 value(
                force->Value.x, force->Value.y, force->Value.z);
            if (force->Torque) {
                if (!maintainAngular)
                    dyn->addTorque(value, physx::PxForceMode::eFORCE);
            } else if (!maintainLinear) {
                dyn->addForce(value, physx::PxForceMode::eFORCE);
            }
        }
        if (maintainLinear) {
            dyn->setLinearVelocity(physx::PxVec3(
                linearTarget.x, linearTarget.y, linearTarget.z));
        }
        if (maintainAngular) {
            dyn->setAngularVelocity(physx::PxVec3(
                angularTarget.x, angularTarget.y, angularTarget.z));
        }
    }
}

void PhysXPhysicsBackend::stepOnce(float dt) {
    const float fixedStep = 1.0f / 60.0f;
    const int MAX_STEPS = 10;
    constexpr float DISTANCE_TOLERANCE = 0.005f;

    if (dt > 0.25f) dt = 0.25f;

    m_accumulator += dt;

    int steps = 0;
    while (m_accumulator >= fixedStep) {
        // 力は描画frameではなく、実際に進む各fixed tickにだけ蓄積する。
        applyBuoyancy();
        applyForces();
        // PxDistanceJoint の spring flag は距離上限そのものを soft constraint にするため、
        // Rope の hard max joint とは分離して張力だけを明示的に加える。
        for (auto& entry : m_constraints) {
            auto constraint = entry.constraint.lock();
            if (!constraint || !constraint->IsA("Rope") || !entry.joint) continue;
            auto rope = std::static_pointer_cast<Rope>(constraint);
            const float stiffness = std::max(0.0f, rope->Stiffness);
            const float damping = std::max(0.0f, rope->Damping);
            if (stiffness <= 0.0f && damping <= 0.0f) continue;

            auto* joint = static_cast<physx::PxDistanceJoint*>(entry.joint);
            physx::PxRigidActor* actor0 = nullptr;
            physx::PxRigidActor* actor1 = nullptr;
            joint->getActors(actor0, actor1);
            if (!actor0 || !actor1 || actor0 == actor1) continue;

            const physx::PxVec3 anchor0 = actor0->getGlobalPose()
                .transform(joint->getLocalPose(physx::PxJointActorIndex::eACTOR0)).p;
            const physx::PxVec3 anchor1 = actor1->getGlobalPose()
                .transform(joint->getLocalPose(physx::PxJointActorIndex::eACTOR1)).p;
            const physx::PxVec3 delta = anchor1 - anchor0;
            const float distanceSquared = delta.magnitudeSquared();
            if (!std::isfinite(distanceSquared) ||
                distanceSquared <= DISTANCE_TOLERANCE * DISTANCE_TOLERANCE) continue;

            const float distance = std::sqrt(distanceSquared);
            const float maximumDistance = joint->getMaxDistance();
            // max より十分内側では完全に slack。ばね・減衰とも押し広げない。
            if (distance < maximumDistance - DISTANCE_TOLERANCE) continue;
            const physx::PxVec3 direction = delta / distance;

            auto* dynamic0 = actor0->is<physx::PxRigidDynamic>();
            auto* dynamic1 = actor1->is<physx::PxRigidDynamic>();
            const bool movable0 = dynamic0 &&
                !(dynamic0->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
            const bool movable1 = dynamic1 &&
                !(dynamic1->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
            const float inverseMass0 = movable0 ? dynamic0->getInvMass() : 0.0f;
            const float inverseMass1 = movable1 ? dynamic1->getInvMass() : 0.0f;
            const float inverseMassSum = inverseMass0 + inverseMass1;
            if (!(inverseMassSum > 0.0f) || !std::isfinite(inverseMassSum)) continue;
            const float effectiveMass = 1.0f / inverseMassSum;

            physx::PxVec3 velocity0(0.0f);
            physx::PxVec3 velocity1(0.0f);
            if (dynamic0)
                velocity0 = physx::PxRigidBodyExt::getVelocityAtPos(*dynamic0, anchor0);
            if (dynamic1)
                velocity1 = physx::PxRigidBodyExt::getVelocityAtPos(*dynamic1, anchor1);
            const float separatingSpeed = (velocity1 - velocity0).dot(direction);
            const float extension = std::max(0.0f, distance - maximumDistance);

            // approaching 時の減衰項は張力を減らすだけにし、負値を clamp することで
            // Rope が圧縮力を発生して slack を押し広げることを防ぐ。
            float tension = stiffness * extension + damping * separatingSpeed;
            if (!std::isfinite(tension) || tension <= 0.0f) continue;

            // 1 substep で許される相対速度/伸び補正を超えないよう有効質量で上限を置く。
            const float stableLimit = effectiveMass * (
                std::max(extension, DISTANCE_TOLERANCE) / (fixedStep * fixedStep) +
                std::max(separatingSpeed, 0.0f) / fixedStep);
            if (!std::isfinite(stableLimit) || stableLimit <= 0.0f) continue;
            tension = std::min(tension, stableLimit);
            const physx::PxVec3 force = direction * tension;
            if (movable0)
                physx::PxRigidBodyExt::addForceAtPos(
                    *dynamic0, force, anchor0, physx::PxForceMode::eFORCE);
            if (movable1)
                physx::PxRigidBodyExt::addForceAtPos(
                    *dynamic1, -force, anchor1, physx::PxForceMode::eFORCE);
        }

        scene->simulate(fixedStep);
        scene->fetchResults(true);
        ++m_simulationTick;

        m_accumulator -= fixedStep;
        steps++;

        // 安全装置の発動
        if (steps >= MAX_STEPS) {
            m_accumulator = 0.0f; // 追いつけない分は「なかったこと」にする（スローモーション化）
            RCBN_WARN("Physics safety break engaged! (Spiral of Death prevented)");
            break;
        }
    }
    m_accumulatorAlpha = std::clamp(m_accumulator / fixedStep, 0.0f, 1.0f);

    // 遅延キューをフラッシュ（fetchResults 完了後の安全ウインドウ）
    for (auto& op : m_pendingOps) {
        auto cube = op.cube.lock();
        if (!cube) continue;
        auto* actor = getActor(cube->m_bodyHandle);
        if (!actor) continue;
        if (op.type == PendingOp::Type::Resize) {
            recreateActor(cube);
        } else if (op.type == PendingOp::Type::SetRotation) {
            physx::PxTransform pose = actor->getGlobalPose();
            pose.q = physx::PxQuat(op.rotation.x, op.rotation.y, op.rotation.z, op.rotation.w);
            actor->setGlobalPose(pose);
        }
    }
    m_pendingOps.clear();
}

void PhysXPhysicsBackend::syncAllCubes() {
    for (auto& entry : cubes) {
        if (auto cube = entry.cube.lock(); cube && !cube->m_weldKinematic) {
            syncCube(*cube);
        }
    }
    syncWeldKinematics();
}

std::shared_ptr<BaseCube> PhysXPhysicsBackend::resolveContactIdentity(
    const void* identity) const {
    if (!identity) return {};
    for (const CubeEntry& entry : cubes) {
        if (entry.cubeRaw != identity) continue;
        auto cube = entry.cube.lock();
        if (cube && cube.get() == identity && cube->m_physicsOwner == m_facade)
            return cube;
    }
    return {};
}

void PhysXPhysicsBackend::dispatchContactEvents() {
    auto pending = std::move(m_pendingContacts);
    m_pendingContacts.clear();
    for (const auto& [firstIdentity, secondIdentity] : pending) {
        auto first = resolveContactIdentity(firstIdentity);
        auto second = resolveContactIdentity(secondIdentity);
        if (!first || !second || !Physics::s_contactCallback) continue;
        Physics::s_contactCallback(first.get(), second.get());
    }
}

void PhysXPhysicsBackend::update(Workspace& workspace, float dt) {
    if (!workspace.PhysicsEnabled) return;
    setGravity(workspace.Gravity);

    // 削除済み Cube を参照する制約を実行前に回収する。これにより Weld だけでなく、すべての2点制約がシーンツリーと PhysX の両方から残らない。
    removeInvalidConstraints(workspace);

    // 0. 削除されたキューブをクリーンアップ（Workspace に存在しなくなったキューブを検出）
    auto it = cubes.begin();
    while (it != cubes.end()) {
        auto cube = it->cube.lock();
        // オブジェクトが消滅したか、Workspace の子孫でなくなった場合のみ削除。
        // actor が nullptr でも削除しない: CanCollide==false のキューブ(例: HumanoidのHead)は
        // 元々 actor を持たないが、Weld で compound に組み込まれると駆動対象(syncPhysics)になる。
        // ここで除外すると syncPhysics が呼ばれなくなり、キネマティック追従が止まってしまう
        if (!cube || cube->findFirstAncestorWorkspace() != &workspace) {
            if (cube) {
                cube->m_bodyHandle = {};
                cube->m_compoundLocalOffset = CFrame();
                cube->m_physicsOwner = nullptr;
            }
            physx::PxRigidActor* actor = it->actor;
            it = cubes.erase(it);
            if (actor) {
                const bool sharedWithOthers = std::any_of(
                    cubes.begin(), cubes.end(),
                    [&](const CubeEntry& entry) { return entry.actor == actor; });
                if (!sharedWithOthers) {
                    scene->removeActor(*actor);
                    actor->release();
                }
            }
            // RCBN_LOG("Cleaned up removed cube from Physics: " << (cube ? cube->Name : "Unknown"));
        } else {
            ++it;
        }
    }

    // 1. 未反映の新入りを登録
    for (auto& inst : workspace.pendingInstances) {
        if (inst && inst->IsA("BaseCube")) {
            auto cube = std::static_pointer_cast<BaseCube>(inst);
            // 削除済み/別Workspaceへ移動済みのキューブはアクターを作らない。
            // pendingInstances には残留しうる(removeCubeはここを掃除しない)ため、
            // ここで弾かないと死んだキューブにアクターが生成され二重releaseでクラッシュする。
            if (cube->findFirstAncestorWorkspace() != static_cast<Instance*>(&workspace)) continue;
            if (cube->m_physicsOwner && cube->m_physicsOwner != m_facade) {
                RCBN_ERROR("Ignoring Cube registered in a foreign Physics world: "
                           << cube->Name);
                continue;
            }
            createActor(cube);
            auto* actor = getActor(cube->m_bodyHandle);
            if (!actor) continue;
            auto existing = std::find_if(
                cubes.begin(), cubes.end(),
                [&](const CubeEntry& entry) { return entry.cubeRaw == cube.get(); });
            if (existing == cubes.end()) {
                cubes.push_back({std::weak_ptr<BaseCube>(cube), cube.get(), actor});
            } else {
                existing->cube = cube;
                existing->actor = actor;
            }
        }
    }
    workspace.pendingInstances.clear();

    // 2. 制約クリーンアップ（参照切れジョイント）
    auto cit = m_constraints.begin();
    while (cit != m_constraints.end()) {
        if (cit->constraint.expired()) {
            if (cit->joint) {
                cit->joint->release();
                cit->joint = nullptr;
            }
            cit = m_constraints.erase(cit);
        } else {
            ++cit;
        }
    }

    // 2b. NoCollision エントリークリーンアップ（inst/c0/c1 いずれかが expired なら除去）。
    // 生き残った Cube はビットを解除しないと eSUPPRESS されたペアが再評価されず
    // 衝突が復活しないため、除去後にapplyCollisionFilterで再適用する
    {
        std::vector<std::shared_ptr<BaseCube>> survivors;
        size_t beforeSize = m_noCollisionEntries.size();
        m_noCollisionEntries.erase(
            std::remove_if(m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
                [&](const NoCollisionEntry& e) {
                    if (!e.inst.expired() && !e.c0.expired() && !e.c1.expired()) return false;
                    if (auto constraint = e.inst.lock()) clearConstraintHandle(*constraint);
                    if (auto c0 = e.c0.lock()) survivors.push_back(c0);
                    if (auto c1 = e.c1.lock()) survivors.push_back(c1);
                    return true;
                }),
            m_noCollisionEntries.end());
        if (m_noCollisionEntries.size() != beforeSize) {
            rebuildNoCollisionPairSet();
            for (auto& c : survivors) applyCollisionFilter(*c);
        }
    }

    // 3. 制約の新規登録（Weld を先に処理して compound を確定させてから Rope/Rod/Motor を生成）
    // Weld を1本ずつ createWeld() すると、同じ連結体をWeld本数分だけ再構築することになる。
    // 先に全Weldを登録し、連結成分ごとに現在のワールド姿勢から一度だけcompoundを作る。
    std::vector<std::shared_ptr<Weld>> pendingWelds;
    for (auto& c : workspace.pendingConstraints) {
        if (!c->IsA("Weld")) continue;
        auto weld = std::static_pointer_cast<Weld>(c);
        auto cube0 = weld->m_cube0.lock();
        auto cube1 = weld->m_cube1.lock();
        if (!cube0 || !cube1 || cube0 == cube1) continue;

        const bool alreadyRegistered = std::any_of(
            m_constraints.begin(), m_constraints.end(),
            [&](const ConstraintEntry& entry) { return entry.constraint.lock() == weld; });
        if (!alreadyRegistered) {
            const auto handle = allocateConstraintHandle();
            weld->m_constraintHandle = handle;
            m_constraints.push_back({std::weak_ptr<Instance>(weld), handle, nullptr});
        }
        pendingWelds.push_back(weld);
    }
    {
        std::unordered_set<BaseCube*> rebuilt;
        for (const auto& weld : pendingWelds) {
            auto cube0 = weld->m_cube0.lock();
            if (!cube0 || rebuilt.contains(cube0.get())) continue;
            auto assembly = Weld::collectAssembly(cube0, workspace);
            if (assembly.size() < 2) continue;
            rebuildGroup(assembly);
            for (const auto& cube : assembly) {
                if (cube) rebuilt.insert(cube.get());
            }
        }
    }
    for (auto& c : workspace.pendingConstraints) {
        if      (c->IsA("Rope"))  createRope(std::static_pointer_cast<Rope>(c));
        else if (c->IsA("Rod"))   createRod(std::static_pointer_cast<Rod>(c));
        else if (c->IsA("Motor")) createMotor(std::static_pointer_cast<Motor>(c));
        else if (c->IsA("BallSocket")) createBallSocket(std::static_pointer_cast<BallSocket>(c));
        else if (c->IsA("NoCollision")) createNoCollision(std::static_pointer_cast<NoCollision>(c));
    }
    workspace.pendingConstraints.clear();

    // body/constraint のpendingを同じ固定stepに反映する。Box3Dと
    // 同じく、登録前に world を進めない。
    stepOnce(dt);
    syncAllCubes();
    dispatchContactEvents();
}

void PhysXPhysicsBackend::removeInvalidConstraints(Workspace& workspace) {
    auto cubeIsInWorkspace = [&workspace](const std::weak_ptr<BaseCube>& cube) {
        auto locked = cube.lock();
        return locked && locked->findFirstAncestorWorkspace() == &workspace;
    };
    auto isInvalid = [&](const std::shared_ptr<Instance>& constraint) {
        if (!constraint) return false;
        // Inventory に戻された Tool の Weld は、制約自身と両端が Workspace 外へ
        // 一緒に移動しているだけである。ここで削除すると二回目の装備時に Weld の
        // 連結情報そのものが失われる。これは Weld の再装備処理だけに適用し、他の
        // 制約は従来どおり endpoint の不整合を回収する。
        if (constraint->IsA("Weld") &&
            constraint->findFirstAncestorWorkspace() != &workspace) return false;
        if (constraint->IsA("Weld")) {
            auto c = std::static_pointer_cast<Weld>(constraint);
            return !cubeIsInWorkspace(c->m_cube0) || !cubeIsInWorkspace(c->m_cube1);
        }
        if (constraint->IsA("Rope")) {
            auto c = std::static_pointer_cast<Rope>(constraint);
            return !cubeIsInWorkspace(c->m_cube0) || !cubeIsInWorkspace(c->m_cube1);
        }
        if (constraint->IsA("Rod")) {
            auto c = std::static_pointer_cast<Rod>(constraint);
            return !cubeIsInWorkspace(c->m_cube0) || !cubeIsInWorkspace(c->m_cube1);
        }
        if (constraint->IsA("Motor")) {
            auto c = std::static_pointer_cast<Motor>(constraint);
            return !cubeIsInWorkspace(c->m_cube0) || !cubeIsInWorkspace(c->m_cube1);
        }
        if (constraint->IsA("BallSocket")) {
            auto c = std::static_pointer_cast<BallSocket>(constraint);
            return !cubeIsInWorkspace(c->m_cube0) || !cubeIsInWorkspace(c->m_cube1);
        }
        if (constraint->IsA("NoCollision")) {
            auto c = std::static_pointer_cast<NoCollision>(constraint);
            return !cubeIsInWorkspace(c->m_cube0) || !cubeIsInWorkspace(c->m_cube1);
        }
        return false;
    };

    // removeConstraint()/onAncestorChanged() はコンテナの変更を伴うため、まず別ベクタに固定してから回収する。
    std::vector<std::shared_ptr<Instance>> stale;
    for (const auto& entry : m_constraints) {
        if (auto constraint = entry.constraint.lock(); isInvalid(constraint)) stale.push_back(constraint);
    }
    for (const auto& entry : m_noCollisionEntries) {
        if (auto constraint = entry.inst.lock(); isInvalid(constraint)) stale.push_back(constraint);
    }
    for (const auto& constraint : workspace.pendingConstraints) {
        if (isInvalid(constraint)) stale.push_back(constraint);
    }

    std::sort(stale.begin(), stale.end());
    stale.erase(std::unique(stale.begin(), stale.end()), stale.end());
    for (const auto& constraint : stale) {
        RCBN_LOG("Unbinding constraint \"" << constraint->Name
                 << "\" because one of its cubes left the Workspace");
        removeConstraint(constraint);
    }
    workspace.pendingConstraints.erase(
        std::remove_if(workspace.pendingConstraints.begin(), workspace.pendingConstraints.end(),
            [&stale](const std::shared_ptr<Instance>& constraint) {
                return std::binary_search(stale.begin(), stale.end(), constraint);
            }),
        workspace.pendingConstraints.end());
}

void PhysXPhysicsBackend::syncWeldKinematics() {
    std::unordered_set<physx::PxRigidActor*> visited;
    for (const CubeEntry& seedEntry : cubes) {
        auto seed = seedEntry.cube.lock();
        physx::PxRigidActor* actor = seedEntry.actor;
        if (!seed || !seed->m_weldKinematic || !actor ||
            !visited.insert(actor).second)
            continue;

        std::vector<std::shared_ptr<BaseCube>> members;
        for (const CubeEntry& entry : cubes) {
            if (entry.actor != actor) continue;
            if (auto member = entry.cube.lock()) members.push_back(member);
        }

        const CFrame currentBody = fromPxTransform(actor->getGlobalPose());
        CFrame driverTarget = currentBody;
        float driverDifference = -std::numeric_limits<float>::infinity();
        const BaseCube* driver = nullptr;
        for (const auto& member : members) {
            if (!member || !member->Anchored) continue;
            const CFrame candidate =
                member->getWorldCFrame() * member->m_compoundLocalOffset.inverse();
            const float difference = cframeDifference(candidate, currentBody);
            if (!std::isfinite(difference)) continue;
            if (difference > driverDifference ||
                (difference == driverDifference && driver &&
                 std::less<const BaseCube*>{}(member.get(), driver))) {
                driverDifference = difference;
                driverTarget = candidate;
                driver = member.get();
            }
        }

        auto* dynamic = actor->is<physx::PxRigidDynamic>();
        if (driver && dynamic &&
            (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
            dynamic->setGlobalPose(toPxTransform(driverTarget));
        }
        const CFrame confirmedBody = fromPxTransform(actor->getGlobalPose());
        for (const auto& member : members) {
            if (member)
                member->setWorldCFrame(confirmedBody * member->m_compoundLocalOffset);
        }
    }
}

void PhysXPhysicsBackend::moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame) {
    if (!member) return;

    // ToolのHandleからWeldで到達できる全Cubeを、Tool配下かどうかに関係なく一体として動かす。
    // Workspace外（Inventory内など）ではWeldを探索できないため、Handle単体を更新する。
    std::vector<std::shared_ptr<BaseCube>> assembly { member };
    if (auto* workspace = member->findFirstAncestorWorkspace()) {
        assembly = Weld::collectAssembly(member, *workspace);
    }

    // actor未作成の初回装備でも、次のcompound構築に正しい姿勢を渡せるよう、
    // Handle基準の同一ワールド変換を全メンバーのCFrameへ先に適用する。
    const CFrame handleBefore = member->getWorldCFrame();
    const CFrame worldDelta = worldCFrame * handleBefore.inverse();
    for (const auto& cube : assembly) {
        if (cube) cube->setWorldCFrame(worldDelta * cube->getWorldCFrame());
    }

    auto* actor = getActor(member->m_bodyHandle);
    if (!actor) return;

    // Weld compoundではactor原点とHandleの姿勢が異なるため、Handleのlocal offsetを逆に
    // 適用して共有actorを移動する。これで全シェイプが剛体として手元へ追従する。
    physx::PxTransform handleTarget(
        physx::PxVec3(worldCFrame.Position.x, worldCFrame.Position.y, worldCFrame.Position.z),
        physx::PxQuat(worldCFrame.Rotation.x, worldCFrame.Rotation.y,
                      worldCFrame.Rotation.z, worldCFrame.Rotation.w));
    handleTarget = handleTarget.getNormalized();
    const physx::PxTransform localOffset =
        toPxTransform(member->m_compoundLocalOffset).getNormalized();
    const physx::PxTransform actorTarget = handleTarget.transform(localOffset.getInverse());
    actor->setGlobalPose(actorTarget);

    // 持っている間は重力・衝突で残った運動量を引き継がない。
    if (auto* dynamic = actor->is<physx::PxRigidDynamic>()) {
        dynamic->setLinearVelocity(physx::PxVec3(0.0f));
        dynamic->setAngularVelocity(physx::PxVec3(0.0f));
    }
}

// Attachment 設定時、アクターローカルのジョイントフレームに Attachment の
// キューブ相対 CFrame を合成する（未設定なら base のまま = 従来のキューブ中心）
static physx::PxTransform composeAttachmentFrame(
    const physx::PxTransform& base,
    const std::weak_ptr<Attachment>& attRef,
    const BaseCube* cube)
{
    auto att = attRef.lock();
    if (!att) return base;
    CFrame rel = att->relativeToAncestor(cube);
    return base * physx::PxTransform(
        physx::PxVec3(rel.Position.x, rel.Position.y, rel.Position.z),
        physx::PxQuat(rel.Rotation.x, rel.Rotation.y, rel.Rotation.z, rel.Rotation.w));
}

void PhysXPhysicsBackend::createRope(const std::shared_ptr<Rope>& rope) {
    if (!rope || rope->m_constraintHandle) return;
    auto c0 = rope->m_cube0.lock();
    auto c1 = rope->m_cube1.lock();
    if (!c0 || !c1) {
        RCBN_WARN("Rope \"" << rope->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    auto* actor0 = getActor(c0->m_bodyHandle);
    auto* actor1 = getActor(c1->m_bodyHandle);
    if (!actor0 || !actor1) {
        RCBN_WARN("Rope \"" << rope->Name << "\": actors not ready");
        return;
    }

    physx::PxTransform frame0 = composeAttachmentFrame(
        toPxTransform(c0->m_compoundLocalOffset), rope->m_attachment0, c0.get());
    physx::PxTransform frame1 = composeAttachmentFrame(
        toPxTransform(c1->m_compoundLocalOffset), rope->m_attachment1, c1.get());
    float dist = rope->MaxDistance;
    if (dist <= 0.0f) {
        auto p0 = actor0->getGlobalPose().transform(frame0).p;
        auto p1 = actor1->getGlobalPose().transform(frame1).p;
        dist = (p1 - p0).magnitude();
    }

    PhysicsConstraintDescriptor descriptor;
    descriptor.type = PhysicsConstraintType::Distance;
    descriptor.bodyA = c0->m_bodyHandle;
    descriptor.bodyB = c1->m_bodyHandle;
    descriptor.localFrameA = fromPxTransform(frame0);
    descriptor.localFrameB = fromPxTransform(frame1);
    descriptor.userData = rope.get();
    descriptor.maxLength = dist;
    descriptor.enableLimit = true;
    descriptor.enableSpring = rope->Stiffness > 0.0f;
    descriptor.tensionOnly = true;
    descriptor.stiffness = rope->Stiffness;
    descriptor.damping = rope->Damping;

    physx::PxDistanceJoint* joint = nullptr;
    if (actor0 != actor1) {
        joint = PxDistanceJointCreate(
            *s_pxPhysics, actor0, toPxTransform(descriptor.localFrameA),
            actor1, toPxTransform(descriptor.localFrameB));
    }
    if (actor0 != actor1 && !joint) return;
    if (joint) {
        joint->setTolerance(0.005f);
        joint->setMaxDistance(descriptor.maxLength);
        joint->setMinDistance(descriptor.minLength);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, descriptor.enableLimit);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED,
            descriptor.enableLimit && !descriptor.tensionOnly);
        // max distance は常に hard constraint。張力ばねは stepOnce() で別途適用する。
        joint->setStiffness(0.0f);
        joint->setDamping(0.0f);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eSPRING_ENABLED, false);
        joint->setConstraintFlag(
            physx::PxConstraintFlag::eCOLLISION_ENABLED, descriptor.collideConnected);
        joint->userData = descriptor.userData;
    }

    const auto handle = allocateConstraintHandle();
    rope->m_constraintHandle = handle;
    m_constraints.push_back({std::weak_ptr<Instance>(rope), handle, joint});
    // // RCBN_LOG("Rope \"" << rope->Name << "\" created, maxDistance=" << dist);
}

void PhysXPhysicsBackend::createRod(const std::shared_ptr<Rod>& rod) {
    if (!rod || rod->m_constraintHandle) return;
    auto c0 = rod->m_cube0.lock();
    auto c1 = rod->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("Rod \"" << rod->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    auto* actor0 = getActor(c0->m_bodyHandle);
    auto* actor1 = getActor(c1->m_bodyHandle);
    if (!actor0 || !actor1) {
        // RCBN_WARN("Rod \"" << rod->Name << "\": actors not ready");
        return;
    }

    physx::PxTransform frame0 = composeAttachmentFrame(
        toPxTransform(c0->m_compoundLocalOffset), rod->m_attachment0, c0.get());
    physx::PxTransform frame1 = composeAttachmentFrame(
        toPxTransform(c1->m_compoundLocalOffset), rod->m_attachment1, c1.get());
    auto p0 = actor0->getGlobalPose().transform(frame0).p;
    auto p1 = actor1->getGlobalPose().transform(frame1).p;
    float dist = (p1 - p0).magnitude();

    PhysicsConstraintDescriptor descriptor;
    descriptor.type = PhysicsConstraintType::Distance;
    descriptor.bodyA = c0->m_bodyHandle;
    descriptor.bodyB = c1->m_bodyHandle;
    descriptor.localFrameA = fromPxTransform(frame0);
    descriptor.localFrameB = fromPxTransform(frame1);
    descriptor.userData = rod.get();
    descriptor.restLength = dist;
    descriptor.minLength = dist;
    descriptor.maxLength = dist;
    descriptor.enableLimit = true;

    physx::PxDistanceJoint* joint = nullptr;
    if (actor0 != actor1) {
        joint = PxDistanceJointCreate(
            *s_pxPhysics, actor0, toPxTransform(descriptor.localFrameA),
            actor1, toPxTransform(descriptor.localFrameB));
    }
    if (actor0 != actor1 && !joint) return;
    if (joint) {
        joint->setTolerance(0.005f);
        joint->setMaxDistance(descriptor.maxLength);
        joint->setMinDistance(descriptor.minLength);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, descriptor.enableLimit);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED,
            descriptor.enableLimit && !descriptor.tensionOnly);
        joint->setConstraintFlag(
            physx::PxConstraintFlag::eCOLLISION_ENABLED, descriptor.collideConnected);
        joint->userData = descriptor.userData;
    }

    const auto handle = allocateConstraintHandle();
    rod->m_constraintHandle = handle;
    m_constraints.push_back({std::weak_ptr<Instance>(rod), handle, joint});
    RCBN_LOG("Rod \"" << rod->Name << "\" created, distance=" << dist);
}

void PhysXPhysicsBackend::createBallSocket(const std::shared_ptr<BallSocket>& bs) {
    if (!bs || bs->m_constraintHandle) return;
    auto c0 = bs->m_cube0.lock();
    auto c1 = bs->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("BallSocket \"" << bs->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    auto* actor0 = getActor(c0->m_bodyHandle);
    auto* actor1 = getActor(c1->m_bodyHandle);
    if (!actor0 || !actor1) {
        // RCBN_WARN("BallSocket \"" << bs->Name << "\": actors not ready");
        return;
    }

    physx::PxTransform frame0 = composeAttachmentFrame(
        toPxTransform(c0->m_compoundLocalOffset), bs->m_attachment0, c0.get());
    physx::PxTransform frame1 = composeAttachmentFrame(
        toPxTransform(c1->m_compoundLocalOffset), bs->m_attachment1, c1.get());

    PhysicsConstraintDescriptor descriptor;
    descriptor.type = PhysicsConstraintType::Spherical;
    descriptor.bodyA = c0->m_bodyHandle;
    descriptor.bodyB = c1->m_bodyHandle;
    descriptor.localFrameA = fromPxTransform(frame0);
    descriptor.localFrameB = fromPxTransform(frame1);
    descriptor.userData = bs.get();

    physx::PxSphericalJoint* joint = nullptr;
    if (actor0 != actor1) {
        joint = PxSphericalJointCreate(
            *s_pxPhysics, actor0, toPxTransform(descriptor.localFrameA),
            actor1, toPxTransform(descriptor.localFrameB));
    }
    if (actor0 != actor1 && !joint) return;
    if (joint) {
        joint->setConstraintFlag(
            physx::PxConstraintFlag::eCOLLISION_ENABLED, descriptor.collideConnected);
        joint->userData = descriptor.userData;
    }

    const auto handle = allocateConstraintHandle();
    bs->m_constraintHandle = handle;
    m_constraints.push_back({std::weak_ptr<Instance>(bs), handle, joint});
    RCBN_LOG("BallSocket \"" << bs->Name << "\" created");
}

void PhysXPhysicsBackend::rebuildNoCollisionPairSet() {
    m_noCollisionPairs.clear();
    for (auto& entry : m_noCollisionEntries) {
        auto c0 = entry.c0.lock();
        auto c1 = entry.c1.lock();
        if (!c0 || !c1) continue;
        const void* p0 = c0.get();
        const void* p1 = c1.get();
        auto normalized = (p0 < p1) ? std::make_pair(p0, p1) : std::make_pair(p1, p0);
        m_noCollisionPairs.insert(normalized);
    }
}

bool PhysXPhysicsBackend::isInNoCollisionPair(const BaseCube* cube) const {
    if (!cube) return false;
    for (auto& entry : m_noCollisionEntries) {
        auto c0 = entry.c0.lock();
        auto c1 = entry.c1.lock();
        if (!c0 || !c1) continue;
        if (c0.get() == cube || c1.get() == cube) return true;
    }
    return false;
}

void PhysXPhysicsBackend::applyCollisionFilter(BaseCube& cube) {
    auto* actor = getActor(cube.m_bodyHandle);
    if (!actor) return;

    const bool shouldHaveBit = isInNoCollisionPair(&cube);
    bool changed = false;

    for (physx::PxU32 i = 0; i < actor->getNbShapes(); i++) {
        physx::PxShape* shape = nullptr;
        actor->getShapes(&shape, 1, i);
        if (!shape || shape->userData != &cube) continue;

        physx::PxFilterData fd = shape->getSimulationFilterData();
        const bool hasBit =
            (fd.word0 & FILTER_WORD0_NOCOLLISION_CANDIDATE) != 0;
        if (hasBit == shouldHaveBit &&
            fd.word1 == cube.m_characterCollisionGroup) continue;

        if (shouldHaveBit) fd.word0 |= FILTER_WORD0_NOCOLLISION_CANDIDATE;
        else                fd.word0 &= ~FILTER_WORD0_NOCOLLISION_CANDIDATE;
        fd.word1 = cube.m_characterCollisionGroup;
        shape->setSimulationFilterData(fd);
        changed = true;
    }

    if (changed) {
        scene->resetFiltering(*actor);
        auto* dyn = actor->is<physx::PxRigidDynamic>();
        if (dyn && !(dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
            dyn->wakeUp();
        }
    }
}

void PhysXPhysicsBackend::refreshCollisionFilter(BaseCube& cube) {
    applyCollisionFilter(cube);
}

void PhysXPhysicsBackend::createNoCollision(const std::shared_ptr<NoCollision>& nc) {
    if (!nc) return;
    auto c0 = nc->m_cube0.lock();
    auto c1 = nc->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("NoCollision \"" << nc->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }

    auto existing = std::find_if(m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
        [&](const NoCollisionEntry& e) { return e.inst.lock() == nc; });
    if (existing == m_noCollisionEntries.end()) {
        const auto handle = allocateConstraintHandle();
        nc->m_constraintHandle = handle;
        m_noCollisionEntries.push_back({
            std::weak_ptr<Instance>(nc), std::weak_ptr<BaseCube>(c0),
            std::weak_ptr<BaseCube>(c1), handle});
    } else {
        nc->m_constraintHandle = existing->handle;
    }

    rebuildNoCollisionPairSet();
    applyCollisionFilter(*c0);
    applyCollisionFilter(*c1);
    RCBN_LOG("NoCollision \"" << nc->Name << "\" created");
}

// Weld 用シェイプ追加ヘルパー。シェイプを実際に追加できた場合のみ true を返す
// （CanCollide==false や ConvexMesh cooking 失敗時は追加されない。呼び出し側で
// shapeDensities 配列の要素数を実際の shape 数と一致させるために使う）
static bool attachShapeToCompound(
    physx::PxPhysics* px,
    const std::shared_ptr<BaseCube>& cube,
    physx::PxRigidDynamic* compound,
    const physx::PxTransform& localOffset,
    physx::PxMaterial* mat)
{
    if (!cube->CanCollide) return false; // 衝突無効パーツは形状を持たない（位置追従のみ。createActor()と同じ規約）

    switch (cube->getPhysicsShape()) {
    case PhysicsShape::Box: {
        physx::PxBoxGeometry geom(cube->Size.x / 2, cube->Size.y / 2, cube->Size.z / 2);
        physx::PxShape* shape = px->createShape(geom, *mat);
        shape->setLocalPose(localOffset);
        shape->userData = cube.get();
        compound->attachShape(*shape);
        shape->release();
        return true;
    }
    case PhysicsShape::Sphere: {
        physx::PxSphereGeometry geom(cube->Size.x / 2.0f);
        physx::PxShape* shape = px->createShape(geom, *mat);
        shape->setLocalPose(localOffset);
        shape->userData = cube.get();
        compound->attachShape(*shape);
        shape->release();
        return true;
    }
    case PhysicsShape::ConvexMesh: {
        auto vertices = cube->getConvexVertices();
        if (vertices.empty()) return false;
        auto verts = toPxVertices(vertices);
        physx::PxCookingParams cookParams(px->getTolerancesScale());
        physx::PxConvexMeshDesc desc;
        desc.points.count  = static_cast<physx::PxU32>(verts.size());
        desc.points.stride = sizeof(physx::PxVec3);
        desc.points.data   = verts.data();
        desc.flags         = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eQUANTIZE_INPUT;
        physx::PxDefaultMemoryOutputStream buf;
        physx::PxConvexMeshCookingResult::Enum result;
        if (!PxCookConvexMesh(cookParams, desc, buf, &result)) {
            // RCBN_WARN("Weld: ConvexMesh cooking failed for: " << cube->Name);
            return false;
        }
        physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
        physx::PxConvexMesh* mesh = px->createConvexMesh(input);
        physx::PxMeshScale scale(physx::PxVec3(cube->Size.x, cube->Size.y, cube->Size.z));
        physx::PxConvexMeshGeometry geom(mesh, scale);
        physx::PxShape* shape = px->createShape(geom, *mat);
        shape->setLocalPose(localOffset);
        shape->userData = cube.get();
        compound->attachShape(*shape);
        shape->release();
        mesh->release();
        return true;
    }
    }
    return false;
}

void PhysXPhysicsBackend::rebuildGroup(const std::vector<std::shared_ptr<BaseCube>>& assembly) {
    if (assembly.empty()) return;

    physx::PxVec3 savedLinearVelocity(0.0f);
    physx::PxVec3 savedAngularVelocity(0.0f);
    bool restoreDynamicVelocity = false;
    for (auto& cube : assembly) {
        if (!cube) continue;
        cube->m_physicsOwner = m_facade;
        auto* actor = getActor(cube->m_bodyHandle);
        auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
        if (!dynamic || (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) continue;
        savedLinearVelocity = dynamic->getLinearVelocity();
        savedAngularVelocity = dynamic->getAngularVelocity();
        restoreDynamicVelocity = true;
        break;
    }

    // 1. アクター破棄前にワールド姿勢を保存
    std::unordered_map<BaseCube*, physx::PxTransform> savedPoses;
    for (auto& cube : assembly) {
        auto* actor = getActor(cube->m_bodyHandle);
        if (actor) {
            // PxTransform::getInverse()/transform() は単位Quaternionを前提とする。
            // compoundの再構築を重ねても丸め誤差が位置の拡大へ変換されないよう、合成前後で正規化する。
            const physx::PxTransform actorPose = actor->getGlobalPose().getNormalized();
            const physx::PxTransform localOffset =
                toPxTransform(cube->m_compoundLocalOffset).getNormalized();
            savedPoses[cube.get()] = actorPose.transform(localOffset).getNormalized();
        } else {
            auto wp = cube->getWorldPosition();
            auto wr = cube->getWorldCFrame().Rotation;
            savedPoses[cube.get()] = physx::PxTransform(
                physx::PxVec3(wp.x, wp.y, wp.z),
                physx::PxQuat(wr.x, wr.y, wr.z, wr.w)
            ).getNormalized();
        }
    }

    const auto originIt = std::find_if(
        assembly.begin(), assembly.end(),
        [](const auto& cube) { return static_cast<bool>(cube); });
    if (originIt == assembly.end()) return;
    const auto& originCube = *originIt;
    const physx::PxTransform originPose =
        savedPoses[originCube.get()].getNormalized();

    // 旧actorをsceneに残したまま、新compoundをscene未登録状態で完成させる。
    physx::PxRigidDynamic* compound =
        s_pxPhysics->createRigidDynamic(originPose);
    if (!compound) {
        RCBN_ERROR("PhysX compound allocation failed; old assembly retained");
        return;
    }
    compound->setSolverIterationCounts(8, 2);
    std::unordered_map<BaseCube*, physx::PxTransform> localOffsets;
    std::vector<physx::PxReal> shapeDensities;
    bool constructionFailed = false;
    bool anyAnchored = false;
    PhysicsLockFlags combinedLockFlags = PhysicsLockFlags::None;
    for (const auto& cube : assembly) {
        if (!cube) continue;
        const physx::PxTransform localOffset = originPose.getInverse()
            .transform(savedPoses[cube.get()]).getNormalized();
        localOffsets[cube.get()] = localOffset;
        physx::PxMaterial* material = getOrCreateMaterial(cube->material);
        const bool attached = material && attachShapeToCompound(
            s_pxPhysics, cube, compound, localOffset, material);
        if (attached)
            shapeDensities.push_back(std::max(cube->MassDensity, 0.01f));
        else if (cube->CanCollide)
            constructionFailed = true;
        anyAnchored = anyAnchored || cube->Anchored;
        combinedLockFlags |= cube->LockFlags;
    }
    if (constructionFailed) {
        compound->release();
        RCBN_ERROR("PhysX compound shape creation failed; old assembly retained");
        return;
    }
    if (anyAnchored) {
        compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
    } else {
        compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
        compound->setRigidDynamicLockFlags(toPxLockFlags(combinedLockFlags));
    }
    if (compound->getNbShapes() > 0) {
        if (!physx::PxRigidBodyExt::updateMassAndInertia(
                *compound, shapeDensities.data(),
                static_cast<physx::PxU32>(shapeDensities.size()))) {
            compound->setMass(1.0f);
            compound->setMassSpaceInertiaTensor(
                physx::PxVec3(1.0f, 1.0f, 1.0f));
        }
    } else {
        compound->setMass(1.0f);
        compound->setMassSpaceInertiaTensor(
            physx::PxVec3(1.0f, 1.0f, 1.0f));
    }
    if (restoreDynamicVelocity && !anyAnchored) {
        compound->setLinearVelocity(savedLinearVelocity);
        compound->setAngularVelocity(savedAngularVelocity);
    }
    compound->userData = originCube.get();
    for (physx::PxU32 index = 0; index < compound->getNbShapes(); ++index) {
        physx::PxShape* shape = nullptr;
        compound->getShapes(&shape, 1, index);
        if (!shape || !shape->userData) continue;
        physx::PxFilterData data = shape->getSimulationFilterData();
        BaseCube* shapeCube = static_cast<BaseCube*>(shape->userData);
        if (isInNoCollisionPair(shapeCube))
            data.word0 |= FILTER_WORD0_NOCOLLISION_CANDIDATE;
        data.word1 = shapeCube->m_characterCollisionGroup;
        shape->setSimulationFilterData(data);
    }

    // 新compound完成後だけ旧actorを交換する。
    std::unordered_set<physx::PxRigidActor*> toRelease;
    std::unordered_set<BaseCube*> assemblyPtrs;
    for (const auto& cube : assembly) {
        if (!cube) continue;
        assemblyPtrs.insert(cube.get());
        if (auto* actor = getActor(cube->m_bodyHandle)) toRelease.insert(actor);
    }
    for (physx::PxRigidActor* actor : toRelease) {
        scene->removeActor(*actor);
        actor->release();
    }
    scene->addActor(*compound);
    for (const auto& cube : assembly) {
        if (!cube) continue;
        setActor(cube->m_bodyHandle, compound);
        cube->m_compoundLocalOffset =
            fromPxTransform(localOffsets[cube.get()]);
        cube->m_physicsOwner = m_facade;
        cube->m_weldKinematic = anyAnchored;
        auto entry = std::find_if(
            cubes.begin(), cubes.end(), [&](const CubeEntry& candidate) {
                return candidate.cubeRaw == cube.get();
            });
        if (entry == cubes.end())
            cubes.push_back({cube, cube.get(), compound});
        else {
            entry->cube = cube;
            entry->actor = compound;
        }
    }

    // 6. assembly 内の cube を参照している Rope/Rod/Motor を再構築
    std::vector<std::shared_ptr<Instance>> constraintsToRebuild;
    for (auto& cEntry : m_constraints) {
        auto inst = cEntry.constraint.lock();
        if (!inst || inst->IsA("Weld")) continue;
        std::shared_ptr<BaseCube> ec0, ec1;
        if (inst->IsA("Rope")) {
            auto r = std::static_pointer_cast<Rope>(inst);
            ec0 = r->m_cube0.lock(); ec1 = r->m_cube1.lock();
        } else if (inst->IsA("Rod")) {
            auto r = std::static_pointer_cast<Rod>(inst);
            ec0 = r->m_cube0.lock(); ec1 = r->m_cube1.lock();
        } else if (inst->IsA("Motor")) {
            auto m = std::static_pointer_cast<Motor>(inst);
            ec0 = m->m_cube0.lock(); ec1 = m->m_cube1.lock();
        } else if (inst->IsA("BallSocket")) {
            auto b = std::static_pointer_cast<BallSocket>(inst);
            ec0 = b->m_cube0.lock(); ec1 = b->m_cube1.lock();
        }
        bool touched = (ec0 && assemblyPtrs.count(ec0.get())) ||
                       (ec1 && assemblyPtrs.count(ec1.get()));
        if (touched) {
            if (cEntry.joint) { cEntry.joint->release(); cEntry.joint = nullptr; }
            clearConstraintHandle(*inst);
            constraintsToRebuild.push_back(inst);
        }
    }
    m_constraints.erase(std::remove_if(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& e) {
            auto i = e.constraint.lock();
            return i && std::find(constraintsToRebuild.begin(),
                                  constraintsToRebuild.end(), i) != constraintsToRebuild.end();
        }), m_constraints.end());
    for (auto& inst : constraintsToRebuild) {
        if (inst->IsA("Rope")) {
            createRope(std::static_pointer_cast<Rope>(inst));
        } else if (inst->IsA("Rod")) {
            createRod(std::static_pointer_cast<Rod>(inst));
        } else if (inst->IsA("Motor")) {
            createMotor(std::static_pointer_cast<Motor>(inst));
        } else if (inst->IsA("BallSocket")) {
            createBallSocket(std::static_pointer_cast<BallSocket>(inst));
        }
    }
}

void PhysXPhysicsBackend::createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace) {
    auto c0 = weld->m_cube0.lock();
    auto c1 = weld->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("Weld \"" << weld->Name << "\": cube refs unresolved");
        return;
    }
    // 自己溶接（同一キューブを両端に指定）は退化構成。compound 構築が破綻し
    // クラッシュするため無視する（ユーザーの設定ミス時の保険）。
    if (c0 == c1) {
        RCBN_WARN("Weld \"" << weld->Name << "\": Cube0 と Cube1 が同一のため無視します");
        return;
    }
    // NOTE: CanCollide==false のキューブ(例: HumanoidのHead)はactorを持たないため、
    // ここでactor存在を要求すると永久にWeldできない。rebuildGroup()はactor未生成の
    // キューブもgetWorldPosition()ベースで救済できるので、actorの有無は問わない。

    // collectAssembly でこの Weld を含む全連結キューブを収集
    // （この Weld は workspace.children に既に存在するので BFS に含まれる）
    auto assembly = Weld::collectAssembly(c0, workspace);

    // グループ全体を 1 compound として再構築
    rebuildGroup(assembly);

    // m_constraints に未登録なら追加
    bool alreadyRegistered = std::any_of(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& e) { return e.constraint.lock() == weld; });
    if (!alreadyRegistered) {
        const auto handle = allocateConstraintHandle();
        weld->m_constraintHandle = handle;
        m_constraints.push_back({std::weak_ptr<Instance>(weld), handle, nullptr});
    }
    // RCBN_LOG("Weld \"" << weld->Name << "\" created (group size: " << assembly.size() << ")");
}

// (1,0,0) を to ベクトルに回転させる最短回転クォータニオンを計算
static physx::PxQuat computeShortestRotationFromX(const physx::PxVec3& to) {
    physx::PxVec3 from(1.0f, 0.0f, 0.0f);
    float d = from.dot(to);
    if (d > 0.999999f) return physx::PxQuat(physx::PxIdentity);
    if (d < -0.999999f) {
        // 180度回転：from と直交する任意軸（ここでは Y軸）周り
        return physx::PxQuat(3.14159265358979f, physx::PxVec3(0, 1, 0));
    }
    physx::PxVec3 c = from.cross(to);
    float s = std::sqrt((1.0f + d) * 2.0f);
    float invs = 1.0f / s;
    return physx::PxQuat(c.x * invs, c.y * invs, c.z * invs, s * 0.5f);
}

void PhysXPhysicsBackend::createMotor(const std::shared_ptr<Motor>& motor) {
    if (!motor || motor->m_constraintHandle) return;
    auto c0 = motor->m_cube0.lock();
    auto c1 = motor->m_cube1.lock();
    if (!c0 || !c1) {
        RCBN_WARN("Motor \"" << motor->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    auto* actor0 = getActor(c0->m_bodyHandle);
    auto* actor1 = getActor(c1->m_bodyHandle);
    if (!actor0 || !actor1) {
        RCBN_WARN("Motor \"" << motor->Name << "\": actors not ready");
        return;
    }

    // ピボット: 各キューブ自身の CFrame から midpoint を使う
    // (Weld コンパウンドに取り込まれた場合でも actor pose ではなく個別座標が正しい)
    // Attachment0/Attachment1 が設定されていればその位置をピボットに使う:
    //   両方設定 → 各キューブ側のピボットをそれぞれの Attachment 位置に
    //   片方のみ → その Attachment 位置を共有ピボットに
    //   未設定   → 従来通り 2 キューブの中点
    physx::PxTransform pose0 = actor0->getGlobalPose();
    physx::PxTransform pose1 = actor1->getGlobalPose();
    auto* sp0 = static_cast<Spatial*>(c0.get());
    auto* sp1 = static_cast<Spatial*>(c1.get());
    physx::PxVec3 p0 { sp0->getWorldCFrame().Position.x,
                        sp0->getWorldCFrame().Position.y,
                        sp0->getWorldCFrame().Position.z };
    physx::PxVec3 p1 { sp1->getWorldCFrame().Position.x,
                        sp1->getWorldCFrame().Position.y,
                        sp1->getWorldCFrame().Position.z };
    physx::PxVec3 pivotWorld = (p0 + p1) * 0.5f;

    auto att0 = motor->m_attachment0.lock();
    auto att1 = motor->m_attachment1.lock();
    physx::PxVec3 pivot0World = pivotWorld;
    physx::PxVec3 pivot1World = pivotWorld;
    if (att0 || att1) {
        auto worldOf = [](const std::shared_ptr<Attachment>& a) {
            Vector3 wp = a->getWorldCFrame().Position;
            return physx::PxVec3(wp.x, wp.y, wp.z);
        };
        if (att0 && att1) {
            pivot0World = worldOf(att0);
            pivot1World = worldOf(att1);
        } else {
            physx::PxVec3 shared = att0 ? worldOf(att0) : worldOf(att1);
            pivot0World = shared;
            pivot1World = shared;
        }
    }

    // motor->Axis は Cube0 基準のローカル方向として解釈し、Cube0 の現在のワールド回転で
    // ワールド方向へ変換する。以前はAxisをワールド固定ベクトルとして扱っていたため、
    // Weldリビルド等(着席時のSeatWeld生成など、本来Motorと無関係な操作)でこのMotorの
    // ジョイントが作り直されるたびに、車体の傾きを無視した軸へ巻き戻ってしまっていた
    Vector3 axisWorldVec = sp0->getWorldCFrame().Rotation.rotate(motor->Axis);
    physx::PxVec3 axisW(axisWorldVec.x, axisWorldVec.y, axisWorldVec.z);
    axisW.normalize();
    physx::PxQuat axisRot = computeShortestRotationFromX(axisW);

    physx::PxTransform frame0 = pose0.transformInv(physx::PxTransform(pivot0World, axisRot));
    physx::PxTransform frame1 = pose1.transformInv(physx::PxTransform(pivot1World, axisRot));

    PhysicsConstraintDescriptor descriptor;
    descriptor.type = PhysicsConstraintType::Revolute;
    descriptor.bodyA = c0->m_bodyHandle;
    descriptor.bodyB = c1->m_bodyHandle;
    descriptor.localFrameA = fromPxTransform(frame0);
    descriptor.localFrameB = fromPxTransform(frame1);
    descriptor.userData = motor.get();
    descriptor.enableMotor = true;
    descriptor.driveVelocity = motor->DriveVelocity;
    descriptor.maxTorque = motor->MaxForce;

    physx::PxRevoluteJoint* joint = nullptr;
    if (actor0 != actor1) {
        joint = PxRevoluteJointCreate(
            *s_pxPhysics, actor0, toPxTransform(descriptor.localFrameA),
            actor1, toPxTransform(descriptor.localFrameB));
    }
    if (actor0 != actor1 && !joint) {
        RCBN_WARN("Motor \"" << motor->Name << "\": PxRevoluteJointCreate failed");
        return;
    }
    if (joint) {
        joint->setRevoluteJointFlag(
            physx::PxRevoluteJointFlag::eDRIVE_ENABLED, descriptor.enableMotor);
        joint->setDriveVelocity(descriptor.driveVelocity);
        joint->setDriveForceLimit(descriptor.maxTorque);
        // Keep Motor's established public MaxForce contract stable across
        // PhysX SDK versions: revolute drive limits are angular impulses per
        // fixed tick, rather than force/torque limits.
        joint->setConstraintFlag(
            physx::PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, false);
        joint->setConstraintFlag(
            physx::PxConstraintFlag::eCOLLISION_ENABLED, descriptor.collideConnected);
        joint->userData = descriptor.userData;
    }

    const auto handle = allocateConstraintHandle();
    motor->m_constraintHandle = handle;
    m_constraints.push_back({std::weak_ptr<Instance>(motor), handle, joint});
    RCBN_LOG("Motor \"" << motor->Name << "\" created at pivot (" << pivotWorld.x << ", " << pivotWorld.y << ", " << pivotWorld.z << ")");
}

void PhysXPhysicsBackend::updateConstraint(
    const std::shared_ptr<Instance>& constraint) {
    if (!constraint) return;

    if (constraint->IsA("Rope")) {
        auto rope = std::static_pointer_cast<Rope>(constraint);
        auto* entry = findConstraintEntry(rope->m_constraintHandle);
        if (!entry || !entry->joint) return;

        auto* joint = static_cast<physx::PxDistanceJoint*>(entry->joint);
        joint->setTolerance(0.005f);
        joint->setMaxDistance(std::max(rope->MaxDistance, 0.005f));
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, false);
        joint->setStiffness(0.0f);
        joint->setDamping(0.0f);
        joint->setDistanceJointFlag(
            physx::PxDistanceJointFlag::eSPRING_ENABLED, false);
        return;
    }

    if (constraint->IsA("Motor")) {
        auto motor = std::static_pointer_cast<Motor>(constraint);
        auto* entry = findConstraintEntry(motor->m_constraintHandle);
        if (!entry || !entry->joint) return;
        auto* joint = static_cast<physx::PxRevoluteJoint*>(entry->joint);
        joint->setDriveVelocity(motor->DriveVelocity);
        joint->setDriveForceLimit(std::max(motor->MaxForce, 0.0f));
    }
}

void PhysXPhysicsBackend::removeConstraint(const std::shared_ptr<Instance>& c) {
    if (!c) return;
    if (c->IsA("NoCollision")) {
        auto ncIt = std::find_if(m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
            [&](const NoCollisionEntry& e) { return e.inst.lock() == c; });
        if (ncIt == m_noCollisionEntries.end()) {
            clearConstraintHandle(*c);
            return;
        }

        auto oldC0 = ncIt->c0.lock();
        auto oldC1 = ncIt->c1.lock();
        clearConstraintHandle(*c);
        m_noCollisionEntries.erase(ncIt);
        rebuildNoCollisionPairSet();
        if (oldC0) applyCollisionFilter(*oldC0);
        if (oldC1) applyCollisionFilter(*oldC1);
        return;
    }

    auto it = std::find_if(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& e) { return e.constraint.lock() == c; });
    if (it == m_constraints.end()) {
        clearConstraintHandle(*c);
        return;
    }

    if (c->IsA("Weld")) {
        auto weld = std::static_pointer_cast<Weld>(c);

        // native body は制約側にキャッシュせず、現在の端点の opaque body handle から解決する。
        auto endpoint = weld->m_cube0.lock();
        auto* candidate = endpoint ? getActor(endpoint->m_bodyHandle) : nullptr;
        if (!candidate) {
            endpoint = weld->m_cube1.lock();
            candidate = endpoint ? getActor(endpoint->m_bodyHandle) : nullptr;
        }
        physx::PxRigidDynamic* oldCompound = nullptr;
        if (candidate) {
            const bool candidateIsLive = candidate && std::any_of(
                cubes.begin(), cubes.end(),
                [&](const CubeEntry& entry) {
                    return !entry.cube.expired() && entry.actor == candidate;
                });
            if (candidateIsLive) oldCompound = candidate->is<physx::PxRigidDynamic>();
        }

        // 1. 旧 compound を共有していた全キューブを収集
        std::vector<std::shared_ptr<BaseCube>> oldGroupCubes;
        if (oldCompound) {
            for (auto& entry : cubes) {
                auto cube = entry.cube.lock();
                if (cube && entry.actor == oldCompound) oldGroupCubes.push_back(cube);
            }
        }

        clearConstraintHandle(*c);

        // どの生存 cube も指していない場合は native actor に触れず登録だけ除去する。
        if (oldGroupCubes.empty()) {
            m_constraints.erase(it);
            return;
        }

        // 先に削除対象 Weld と旧グループに接続する joint 制約を登録から外す。
        // 分割途中では片側の actor がまだ未生成になり得るため、全成分の再構築後にまとめて戻す。
        m_constraints.erase(it);
        std::unordered_set<BaseCube*> oldPtrs;
        for (const auto& cube : oldGroupCubes) oldPtrs.insert(cube.get());
        std::vector<std::shared_ptr<Instance>> constraintsToRebuild;
        for (auto& entry : m_constraints) {
            auto inst = entry.constraint.lock();
            if (!inst || inst->IsA("Weld")) continue;

            std::shared_ptr<BaseCube> ec0, ec1;
            if (inst->IsA("Rope")) {
                auto value = std::static_pointer_cast<Rope>(inst);
                ec0 = value->m_cube0.lock(); ec1 = value->m_cube1.lock();
            } else if (inst->IsA("Rod")) {
                auto value = std::static_pointer_cast<Rod>(inst);
                ec0 = value->m_cube0.lock(); ec1 = value->m_cube1.lock();
            } else if (inst->IsA("Motor")) {
                auto value = std::static_pointer_cast<Motor>(inst);
                ec0 = value->m_cube0.lock(); ec1 = value->m_cube1.lock();
            } else if (inst->IsA("BallSocket")) {
                auto value = std::static_pointer_cast<BallSocket>(inst);
                ec0 = value->m_cube0.lock(); ec1 = value->m_cube1.lock();
            }
            const bool touched = (ec0 && oldPtrs.contains(ec0.get())) ||
                                 (ec1 && oldPtrs.contains(ec1.get()));
            if (!touched) continue;
            if (entry.joint) {
                entry.joint->release();
                entry.joint = nullptr;
            }
            clearConstraintHandle(*inst);
            constraintsToRebuild.push_back(inst);
        }
        m_constraints.erase(
            std::remove_if(m_constraints.begin(), m_constraints.end(),
                [&](const ConstraintEntry& entry) {
                    auto inst = entry.constraint.lock();
                    return inst && std::find(
                        constraintsToRebuild.begin(), constraintsToRebuild.end(), inst)
                        != constraintsToRebuild.end();
                }),
            m_constraints.end());

        {
            // 旧 compound を破棄（cubes テーブルで生存確認済み）
            scene->removeActor(*oldCompound);
            oldCompound->release();

            // 全キューブの body handle/offset をリセット
            for (auto& cube : oldGroupCubes) {
                cube->m_bodyHandle = {};
                cube->m_compoundLocalOffset = CFrame();
            }
            for (auto& entry : cubes) {
                auto cube = entry.cube.lock();
                if (cube && oldPtrs.count(cube.get())) entry.actor = nullptr;
            }
        }

        // 2. 旧グループを残存 Weld で連結成分に分割し、各成分を再構築
        if (!oldGroupCubes.empty()) {
            // Workspace から既に除去されたキューブ(cubes に未登録)は BFS でグループへ
            // 引き戻さない。削除カスケード中に removeCube 済みのキューブへここでbodyを
            // 再代入してしまうと、cubes にエントリが無いため後続の removeCube の共有判定
            // から漏れて compound が release され、そのキューブのデストラクタが
            // 解放済みbodyに対して release() を呼びアクセス違反になる
            auto isRegistered = [this](const std::shared_ptr<BaseCube>& c) -> bool {
                for (auto& e : cubes) {
                    if (e.cube.lock() == c) return true;
                }
                return false;
            };
            std::unordered_set<BaseCube*> processed;
            for (auto& startCube : oldGroupCubes) {
                if (processed.count(startCube.get())) continue;

                // BFS（m_constraints の残 Weld のみ使用）
                std::vector<std::shared_ptr<BaseCube>> subGroup;
                std::queue<std::shared_ptr<BaseCube>> bfsQ;
                bfsQ.push(startCube);
                processed.insert(startCube.get());

                while (!bfsQ.empty()) {
                    auto current = bfsQ.front(); bfsQ.pop();
                    subGroup.push_back(current);
                    for (auto& cEntry : m_constraints) {
                        auto inst = cEntry.constraint.lock();
                        if (!inst || !inst->IsA("Weld")) continue;
                        auto ew = std::static_pointer_cast<Weld>(inst);
                        auto ec0 = ew->m_cube0.lock();
                        auto ec1 = ew->m_cube1.lock();
                        std::shared_ptr<BaseCube> nb;
                        if      (ec0 == current && ec1 && !processed.count(ec1.get())) nb = ec1;
                        else if (ec1 == current && ec0 && !processed.count(ec0.get())) nb = ec0;
                        if (nb && isRegistered(nb)) { processed.insert(nb.get()); bfsQ.push(nb); }
                    }
                }

                if (subGroup.size() == 1) {
                    // 単独 cube → 独立アクターを再生成
                    createActor(subGroup[0]);
                    for (auto& entry : cubes) {
                        if (entry.cube.lock() == subGroup[0]) {
                            entry.cubeRaw = subGroup[0].get();
                            entry.actor = getActor(subGroup[0]->m_bodyHandle);
                        }
                    }
                } else {
                    rebuildGroup(subGroup);
                }
            }
        }

        for (auto& inst : constraintsToRebuild) {
            if (inst->IsA("Rope")) {
                createRope(std::static_pointer_cast<Rope>(inst));
            } else if (inst->IsA("Rod")) {
                createRod(std::static_pointer_cast<Rod>(inst));
            } else if (inst->IsA("Motor")) {
                createMotor(std::static_pointer_cast<Motor>(inst));
            } else if (inst->IsA("BallSocket")) {
                createBallSocket(std::static_pointer_cast<BallSocket>(inst));
            }
        }
        return;
    }

    // Weld 以外: joint を解放してエントリーを削除
    if (it->joint) it->joint->release();
    clearConstraintHandle(*c);
    m_constraints.erase(it);
}
