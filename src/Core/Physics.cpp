#include "include/Core/Physics.hpp"
#include "include/Util/Logger.hpp"
#include <include/Instances/Spatial.hpp>
#include <include/PhysX/cooking/PxCooking.h>
#include <include/Math/Units.hpp>
#include <unordered_set>
#include <algorithm>
#include <queue>
#include <cmath>
#include <include/Instances/LiquidCube.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Instances/Force.hpp>
#include <include/Instances/BallSocket.hpp>
#include <include/Instances/NoCollision.hpp>

// ===================================================
//  static メンバ定義
// ===================================================
physx::PxFoundation*            Physics::s_foundation  = nullptr;
physx::PxPhysics*               Physics::s_pxPhysics   = nullptr;
physx::PxDefaultCpuDispatcher*  Physics::s_dispatcher  = nullptr;
physx::PxDefaultAllocator       Physics::s_allocator;
physx::PxDefaultErrorCallback   Physics::s_errorCallback;
int                             Physics::s_refCount    = 0;
std::function<void(BaseCube*, BaseCube*)> Physics::s_contactCallback;

// ===================================================
//  衝突通知コールバック
// ===================================================
struct RCBNContactCallback : physx::PxSimulationEventCallback {
    void onContact(const physx::PxContactPairHeader& header,
                   const physx::PxContactPair*, physx::PxU32) override {
        // 削除済みアクターに対する最後のcontactイベントの可能性があるため、userDataを読む前に弾く
        if (header.flags & (physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0 |
                             physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
            return;
        // userData は BaseCube だけでなく Terrain 等も格納されるため(TerrainStreamer.cpp参照)、
        // Instance 経由で IsA チェックしてからでないと BaseCube* として安全に解釈できない
        auto* instA = static_cast<Instance*>(header.actors[0]->userData);
        auto* instB = static_cast<Instance*>(header.actors[1]->userData);
        if (!instA || !instB || !instA->IsA("BaseCube") || !instB->IsA("BaseCube"))
            return;
        auto* a = static_cast<BaseCube*>(instA);
        auto* b = static_cast<BaseCube*>(instB);
        if (a && b && Physics::s_contactCallback)
            Physics::s_contactCallback(a, b);
    }
    void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
    void onWake(physx::PxActor**, physx::PxU32) override {}
    void onSleep(physx::PxActor**, physx::PxU32) override {}
    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
    void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}
};

// NoCollision の対象候補ビット。両シェイプに立っている場合のみフィルターコールバックに回す
static constexpr physx::PxU32 FILTER_WORD0_NOCOLLISION_CANDIDATE = 1u;

// MaintainVelocity が有効な Force を持つ間は、外力を受けず目標速度だけを維持する。
static bool hasEnabledMaintainVelocityForce(const BaseCube& cube) {
    for (const auto& entry : cube.children) {
        const auto& child = entry.second;
        if (!child || !child->IsA("Force")) continue;
        const auto* force = static_cast<const Force*>(child.get());
        if (force->Enabled && force->MaintainVelocity) return true;
    }
    return false;
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

void Physics::setGravity(const Vector3& g) {
    if (scene) scene->setGravity(physx::PxVec3(g.x, g.y, g.z));
}

Vector3 Physics::getGravity() const {
    if (!scene) return Vector3(0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f);
    physx::PxVec3 g = scene->getGravity();
    return Vector3(g.x, g.y, g.z);
}

void Physics::init() {
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
    if (!s_pxPhysics) return;

    physx::PxSceneDesc sceneDesc(s_pxPhysics->getTolerancesScale());
    sceneDesc.gravity               = physx::PxVec3(0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f);
    sceneDesc.cpuDispatcher         = s_dispatcher;
    sceneDesc.filterShader          = rcbnFilterShader;
    m_contactCallback               = new RCBNContactCallback();
    sceneDesc.simulationEventCallback = m_contactCallback;
    m_filterCallback                = new RCBNFilterCallback();
    m_filterCallback->pairs         = &m_noCollisionPairs;
    sceneDesc.filterCallback        = m_filterCallback;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
    scene = s_pxPhysics->createScene(sceneDesc);
}

Physics::~Physics() {
    if (scene) {
        // 制約ジョイントを先にリリース
        for (auto& entry : m_constraints) {
            if (entry.joint) {
                entry.joint->release();
                entry.joint = nullptr;
            }
        }
        m_constraints.clear();

        std::unordered_set<physx::PxRigidActor*> released;
        for (auto& entry : cubes) {
            if (entry.actor && released.find(entry.actor) == released.end()) {
                scene->removeActor(*entry.actor);
                entry.actor->release();
                released.insert(entry.actor);
            }
            if (auto c = entry.cube.lock()) {
                c->actor = nullptr;
            }
            entry.actor = nullptr;
        }
        cubes.clear();
        scene->release();
        scene = nullptr;
    }
    delete m_contactCallback;
    m_contactCallback = nullptr;
    delete m_filterCallback;
    m_filterCallback = nullptr;

    // 最後のインスタンスが共有リソースを解放
    --s_refCount;
    if (s_refCount == 0) {
        PxCloseExtensions();
        if (s_dispatcher) { s_dispatcher->release(); s_dispatcher = nullptr; }
        if (s_pxPhysics)  { s_pxPhysics->release();  s_pxPhysics  = nullptr; }
        if (s_foundation) { s_foundation->release();  s_foundation = nullptr; }
    }
}

void Physics::createActor(const std::shared_ptr<BaseCube>& cube) {
    cube->m_weldKinematic = false; // 単独アクター生成時はWeldメンバーではない
    if (cube->actor) return; // 二重登録防止

    // 初期姿勢（親Model等の変換を含むワールド姿勢。syncPhysics/rebuildGroupと整合させる）
    CFrame wcf = cube->getWorldCFrame();
    physx::PxTransform transform(
        physx::PxVec3(wcf.Position.x, wcf.Position.y, wcf.Position.z),
        physx::PxQuat(wcf.Rotation.x, wcf.Rotation.y, wcf.Rotation.z, wcf.Rotation.w)
    );

    physx::PxRigidActor* actor = nullptr;
    if (cube->Anchored) {
        physx::PxRigidDynamic* kin = s_pxPhysics->createRigidDynamic(transform);
        kin->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
        actor = kin;
    } else {
        physx::PxRigidDynamic* dynamicActor = s_pxPhysics->createRigidDynamic(transform);
        dynamicActor->setRigidDynamicLockFlags(cube->LockFlags);
        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
        dynamicActor->setSolverIterationCounts(8, 2);
        actor = dynamicActor;
    }

    if (cube->CanCollide) {
        physx::PxMaterial* pxMat = getOrCreateMaterial(cube->material);

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
            auto verts = cube->getConvexVertices();
            physx::PxCookingParams cookParams(s_pxPhysics->getTolerancesScale());
            physx::PxConvexMeshDesc desc;
            desc.points.count  = static_cast<physx::PxU32>(verts.size());
            desc.points.stride = sizeof(physx::PxVec3);
            desc.points.data   = verts.data();
            desc.flags         = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eQUANTIZE_INPUT;
            physx::PxDefaultMemoryOutputStream buf;
            physx::PxConvexMeshCookingResult::Enum result;
            if (!PxCookConvexMesh(cookParams, desc, buf, &result)) {
                // RCBN_WARN("ConvexMesh cooking failed for: " << cube->Name);
                actor->release();
                return;
            }
            physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
            physx::PxConvexMesh* mesh = s_pxPhysics->createConvexMesh(input);
            physx::PxMeshScale scale(physx::PxVec3(cube->Size.x, cube->Size.y, cube->Size.z));
            physx::PxConvexMeshGeometry geom(mesh, scale);
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
            if (shape) shape->userData = cube.get();
            mesh->release();
            break;
        }
        }
    }

    // NoCollision の対象なら新規シェイプに候補ビットを立てる（新規actorのためresetFiltering不要）
    if (isInNoCollisionPair(cube.get())) {
        for (physx::PxU32 i = 0; i < actor->getNbShapes(); i++) {
            physx::PxShape* shape = nullptr;
            actor->getShapes(&shape, 1, i);
            if (!shape || shape->userData != cube.get()) continue;
            physx::PxFilterData fd = shape->getSimulationFilterData();
            fd.word0 |= FILTER_WORD0_NOCOLLISION_CANDIDATE;
            shape->setSimulationFilterData(fd);
        }
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
    scene->addActor(*actor);
    actor->userData = cube.get(); // レイキャスト等で逆引きできるようにポインタを保持
    cube->actor = actor; // BaseCube側に参照を戻す
}

bool Physics::raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, physx::PxRigidActor* ignoreActor) {
    if (!scene) return false;

    physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
    physx::PxVec3 pxDir(direction.x, direction.y, direction.z);
    
    if (pxDir.magnitudeSquared() < 1e-6f) return false;
    pxDir.normalize();

    // 複数のヒットを想定（自分自身を突き抜けるため）
    const physx::PxU32 maxHits = 16;
    physx::PxRaycastHit hitBuffer[maxHits];
    physx::PxRaycastBuffer buf(hitBuffer, maxHits);

    bool status = scene->raycast(pxOrigin, pxDir, maxDistance, buf);

    if (status) {
        // ヒットしたアクターを走査し、無視対象以外を見つける
        physx::PxRaycastHit* bestHit = nullptr;

        // 通常のブロッキングヒットを確認
        if (buf.hasBlock) {
            if (buf.block.actor != ignoreActor) {
                bestHit = &buf.block;
            }
        }

        // touchesはBVH走査順で距離順ではない（フィルタコールバック無しだと全ヒットが
        // touchesに入りhasBlockは立たない）ため、全走査して最近接を選ぶ
        for (physx::PxU32 i = 0; i < buf.nbTouches; i++) {
            if (buf.touches[i].actor == ignoreActor) continue;
            if (!bestHit || buf.touches[i].distance < bestHit->distance) {
                bestHit = &buf.touches[i];
            }
        }

        if (bestHit) {
            hitResult.hit = true;
            hitResult.distance = bestHit->distance;
            hitResult.position = Vector3(bestHit->position.x, bestHit->position.y, bestHit->position.z);
            hitResult.normal   = Vector3(bestHit->normal.x,   bestHit->normal.y,   bestHit->normal.z);
            if (bestHit->actor && bestHit->actor->userData) {
                hitResult.instance = static_cast<Instance*>(bestHit->actor->userData);
            } else {
                hitResult.instance = nullptr;
            }
            return true;
        }
    }

    hitResult.hit = false;
    return false;
}

physx::PxMaterial* Physics::getOrCreateMaterial(const Material& m) {
    auto q = [](float v){ return (int)std::lround(v * 1000.0f); };
    MatKey key{ q(m.staticFriction), q(m.dynamicFriction), q(m.restitution) };
    auto it = materialCache.find(key);
    if (it != materialCache.end()) return it->second;

    physx::PxMaterial* pxMat = s_pxPhysics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
    materialCache[key] = pxMat;
    return pxMat;
}

void Physics::enqueueResize(const std::shared_ptr<BaseCube>& cube) {
    m_pendingOps.push_back({ PendingOp::Type::Resize, std::weak_ptr<BaseCube>(cube), {} });
}

void Physics::enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rot) {
    m_pendingOps.push_back({ PendingOp::Type::SetRotation, std::weak_ptr<BaseCube>(cube), rot });
}

void Physics::recreateActor(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;

    physx::PxTransform savedPose(physx::PxIdentity);
    physx::PxVec3 savedLinearVelocity(0.0f);
    physx::PxVec3 savedAngularVelocity(0.0f);
    bool restorePose = false;
    bool restoreDynamicVelocity = false;
    if (auto* dynamic = cube->actor ? cube->actor->is<physx::PxRigidDynamic>() : nullptr) {
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
    // cube->actor が解放済みのダングリングポインタとして残ってしまう(暗黙的な溶接解除+UAFの原因)。
    // 共有中と判定できたら rebuildGroup() でグループ全体を再構築する。
    if (cube->actor) {
        std::vector<std::shared_ptr<BaseCube>> sharedGroup;
        for (auto& entry : cubes) {
            if (entry.actor == cube->actor) {
                if (auto c = entry.cube.lock()) sharedGroup.push_back(c);
            }
        }
        if (sharedGroup.size() > 1) {
            rebuildGroup(sharedGroup); // cubes/m_constraints(Weld/Rope/Rod/Motor)の同期も内部で完結
            return;
        }
    }

    if (cube->actor) {
        scene->removeActor(*cube->actor);
        cube->actor->release();
        cube->actor = nullptr;
    }
    createActor(cube);

    if (cube->actor && restorePose) {
        cube->actor->setGlobalPose(savedPose);
        if (restoreDynamicVelocity) {
            if (auto* dynamic = cube->actor->is<physx::PxRigidDynamic>()) {
                if (!(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
                    dynamic->setLinearVelocity(savedLinearVelocity);
                    dynamic->setAngularVelocity(savedAngularVelocity);
                }
            }
        }
    }

    // cubes 配列内の古いエントリを新アクターに同期（同期しないと cleanup ループが
    // 解放済みの古いポインタに対して二重に removeActor/release してしまう）
    for (auto& entry : cubes) {
        if (entry.cube.lock() == cube) {
            entry.actor = cube->actor;
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
            auto r = std::static_pointer_cast<Rope>(inst);
            r->m_joint = nullptr;
            createRope(r);
        } else if (inst->IsA("Rod")) {
            auto r = std::static_pointer_cast<Rod>(inst);
            r->m_joint = nullptr;
            createRod(r);
        } else if (inst->IsA("Motor")) {
            auto m = std::static_pointer_cast<Motor>(inst);
            m->m_joint = nullptr;
            createMotor(m);
        } else if (inst->IsA("BallSocket")) {
            auto b = std::static_pointer_cast<BallSocket>(inst);
            b->m_joint = nullptr;
            createBallSocket(b);
        }
    }
}

void Physics::clearCubes() {
    // 1. ジョイントを解放（Weld の compound は cubes ループで解放）
    for (auto& entry : m_constraints) {
        if (entry.joint) {
            entry.joint->release();
            entry.joint = nullptr;
        }
        if (auto c = entry.constraint.lock()) {
            if (c->IsA("Weld")) {
                auto w = std::static_pointer_cast<Weld>(c);
                // compound 本体は cubes ループで release されるためポインタ無効化のみ
                w->m_compound = nullptr;
            }
        }
    }
    m_constraints.clear();

    // 2. cube actor を release（compound は最初の参照で release し以降スキップ）
    std::unordered_set<physx::PxRigidActor*> released;
    for (auto& entry : cubes) {
        if (entry.actor && released.find(entry.actor) == released.end()) {
            if (scene) scene->removeActor(*entry.actor);
            entry.actor->release();
            released.insert(entry.actor);
        }
        if (auto c = entry.cube.lock()) {
            c->actor = nullptr;
            c->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity);
        }
    }
    cubes.clear();
    m_pendingOps.clear();
}

void Physics::removeCube(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;

    physx::PxRigidActor* a = cube->actor;
    cube->actor = nullptr;
    cube->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity);

    if (a) {
        // 同じ actor を共有している cube が他にいないか確認（compound の場合）
        bool sharedWithOthers = false;
        for (auto& entry : cubes) {
            auto other = entry.cube.lock();
            if (other && other != cube && other->actor == a) {
                sharedWithOthers = true;
                break;
            }
        }
        if (!sharedWithOthers) {
            if (scene) scene->removeActor(*a);
            a->release();
        }
    }

    // cubes ベクターから自分のエントリーを削除
    auto it = std::find_if(cubes.begin(), cubes.end(), [&](const CubeEntry& entry) {
        return entry.cube.lock() == cube;
    });
    if (it != cubes.end()) {
        cubes.erase(it);
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

float Physics::aabbOverlapVolume(const Vector3& posA, const Vector3& sizeA, const Vector3& posB, const Vector3& sizeB) {
    Vector3 aMin = posA - sizeA * 0.5f, aMax = posA + sizeA * 0.5f;
    Vector3 bMin = posB - sizeB * 0.5f, bMax = posB + sizeB * 0.5f;
    float ox = std::max(0.0f, std::min(aMax.x, bMax.x) - std::max(aMin.x, bMin.x));
    float oy = std::max(0.0f, std::min(aMax.y, bMax.y) - std::max(aMin.y, bMin.y));
    float oz = std::max(0.0f, std::min(aMax.z, bMax.z) - std::max(aMin.z, bMin.z));
    return ox * oy * oz;
}

BaseCube* Physics::findOverlapping(const BaseCube& cube, const std::string& className, float margin) const {
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

void Physics::applyBuoyancy() {
    if (!scene) return;
    physx::PxVec3 g = scene->getGravity();
    Vector3 gVec(g.x, g.y, g.z);

    // 液体を収集
    std::vector<BaseCube*> liquids;
    for (auto& e : cubes)
        if (auto c = e.cube.lock())
            if (c->IsA("LiquidCube")) liquids.push_back(c.get());
    if (liquids.empty()) {
        for (auto& e : cubes) {
            auto cube = e.cube.lock();
            if (!cube || !cube->actor || !hasEnabledMaintainVelocityForce(*cube)) continue;
            auto* dyn = cube->actor->is<physx::PxRigidDynamic>();
            if (!dyn || (dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) continue;
            dyn->setLinearDamping(0.0f);
            dyn->setAngularDamping(0.0f);
        }
        return;
    }

    for (auto& e : cubes) {
        auto cube = e.cube.lock();
        if (!cube || !cube->actor) continue;
        auto* dyn = cube->actor->is<physx::PxRigidDynamic>();
        if (!dyn || (dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) continue;
        if (hasEnabledMaintainVelocityForce(*cube)) {
            dyn->setLinearDamping(0.0f);
            dyn->setAngularDamping(0.0f);
            continue;
        }
        if (cube->IsA("LiquidCube")) continue;

        Vector3 cs = cube->Size;
        float cubeVol = std::max(cs.x * cs.y * cs.z, 1e-4f);

        // サンプル点を物体ローカル空間に均等配置し、物体のワールド回転を適用する。
        // これにより、液体側は軸平行ボックスのまま、浮力を受ける物体側だけが OBB になる。
        const CFrame cubeWorld = cube->getWorldCFrame();
        constexpr int GRID = BUOYANCY_SAMPLE_RES;
        constexpr int SAMPLE_COUNT = GRID * GRID * GRID;
        const float sampleVolume = cubeVol / static_cast<float>(SAMPLE_COUNT);
        float submergedVolume = 0.0f;

        for (int ix = 0; ix < GRID; ix++) {
            for (int iy = 0; iy < GRID; iy++) {
                for (int iz = 0; iz < GRID; iz++) {
                    Vector3 localPos(
                        ((ix + 0.5f) / GRID - 0.5f) * cs.x,
                        ((iy + 0.5f) / GRID - 0.5f) * cs.y,
                        ((iz + 0.5f) / GRID - 0.5f) * cs.z
                    );
                    Vector3 pos = cubeWorld.pointToWorld(localPos);
                    bool isSubmerged = false;
                    float sampleDensity = 0.0f;

                    for (BaseCube* lq : liquids) {
                        Vector3 lMin = lq->getWorldPosition() - lq->Size * 0.5f;
                        Vector3 lMax = lq->getWorldPosition() + lq->Size * 0.5f;
                        if (pos.x >= lMin.x && pos.x <= lMax.x &&
                            pos.y >= lMin.y && pos.y <= lMax.y &&
                            pos.z >= lMin.z && pos.z <= lMax.z) {
                            isSubmerged = true;
                            sampleDensity += std::max(0.0f, static_cast<LiquidCube*>(lq)->Density);
                        }
                    }

                    if (isSubmerged) submergedVolume += sampleVolume;
                    if (sampleDensity <= 0.0f) continue;

                    // 各セルが担当する体積分の浮力を、重心から少し離した位置へ適用する。
                    // 合計浮力は保ったまま、サンプル位置の偏りによる復元トルクを穏やかにする。
                    const float buoyantVolume = sampleVolume * sampleDensity;
                    physx::PxVec3 f(-gVec.x * buoyantVolume,
                                    -gVec.y * buoyantVolume,
                                    -gVec.z * buoyantVolume);
                    const Vector3 torqueReducedPos = cubeWorld.Position +
                        (pos - cubeWorld.Position) * BUOYANCY_TORQUE_SCALE;
                    physx::PxVec3 pxPos(torqueReducedPos.x, torqueReducedPos.y, torqueReducedPos.z);
                    physx::PxRigidBodyExt::addForceAtPos(*dyn, f, pxPos, physx::PxForceMode::eFORCE);
                }
            }
        }

        // 水没割合に比例した PhysX ダンピング（指数減衰で安定して素早く収束）。
        // 水の外（frac==0）では 0 に戻すので、飛び出した瞬間も含めて全フレームで設定する。
        const float frac = std::min(submergedVolume / cubeVol, 1.0f);
        dyn->setLinearDamping (LIQUID_LINEAR_DAMPING  * frac);
        dyn->setAngularDamping(LIQUID_ANGULAR_DAMPING * frac);
    }
}

void Physics::applyForces() {
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (!cube || !cube->actor) continue;
        auto* dyn = cube->actor->is<physx::PxRigidDynamic>();
        if (!dyn || (dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) continue;

        const bool maintainVelocity = hasEnabledMaintainVelocityForce(*cube);
        dyn->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, maintainVelocity);

        for (auto const& [name, child] : cube->children) {
            if (!child || !child->IsA("Force")) continue;
            auto* force = static_cast<Force*>(child.get());
            if (!force->Enabled) continue;
            if (maintainVelocity && !force->MaintainVelocity) continue;

            physx::PxVec3 v(force->Value.x, force->Value.y, force->Value.z);
            if (force->MaintainVelocity) {
                // 維持モード: 目標速度(線速度/角速度)を毎フレーム設定して保つ
                if (force->Torque) dyn->setAngularVelocity(v);
                else               dyn->setLinearVelocity(v);
            } else {
                // 加算モード: 力/トルクとして加える（重力・浮力など他の力と合成される）
                if (force->Torque) dyn->addTorque(v, physx::PxForceMode::eFORCE);
                else               dyn->addForce(v, physx::PxForceMode::eFORCE);
            }
        }
    }
}

void Physics::stepOnce(float dt) {
    const float fixedStep = 1.0f / 60.0f;
    const int MAX_STEPS = 10;

    if (dt > 0.25f) dt = 0.25f;

    m_accumulator += dt;

    applyBuoyancy();  // 浮力を addForce（次の simulate で消費される）
    applyForces();    // Forceインスタンスの力/トルク/速度維持（同上）

    int steps = 0;
    while (m_accumulator >= fixedStep) {
        scene->simulate(fixedStep);
        scene->fetchResults(true);

        m_accumulator -= fixedStep;
        steps++;

        // 安全装置の発動
        if (steps >= MAX_STEPS) {
            m_accumulator = 0.0f; // 追いつけない分は「なかったこと」にする（スローモーション化）
            RCBN_WARN("Physics safety break engaged! (Spiral of Death prevented)");
            break;
        }
    }

    // 遅延キューをフラッシュ（fetchResults 完了後の安全ウインドウ）
    for (auto& op : m_pendingOps) {
        auto cube = op.cube.lock();
        if (!cube || !cube->actor) continue;
        if (op.type == PendingOp::Type::Resize) {
            recreateActor(cube);
        } else if (op.type == PendingOp::Type::SetRotation) {
            physx::PxTransform pose = cube->actor->getGlobalPose();
            pose.q = physx::PxQuat(op.rotation.x, op.rotation.y, op.rotation.z, op.rotation.w);
            cube->actor->setGlobalPose(pose);
        }
    }
    m_pendingOps.clear();
}

void Physics::syncAllCubes() {
    for (auto& entry : cubes) {
        if (auto cube = entry.cube.lock()) {
            cube->syncPhysics();
        }
    }
}

void Physics::update(Workspace& workspace, float dt) {
    if (!workspace.PhysicsEnabled) return;
    setGravity(workspace.Gravity);

    // 削除済み Cube を参照する制約を実行前に回収する。これにより Weld だけでなく、すべての2点制約がシーンツリーと PhysX の両方から残らない。
    removeInvalidConstraints(workspace);

    stepOnce(dt);

    // 0. 削除されたキューブをクリーンアップ（Workspace に存在しなくなったキューブを検出）
    auto it = cubes.begin();
    while (it != cubes.end()) {
        auto cube = it->cube.lock();
        // オブジェクトが消滅したか、Workspace の子孫でなくなった場合のみ削除。
        // actor が nullptr でも削除しない: CanCollide==false のキューブ(例: HumanoidのHead)は
        // 元々 actor を持たないが、Weld で compound に組み込まれると駆動対象(syncPhysics)になる。
        // ここで除外すると syncPhysics が呼ばれなくなり、キネマティック追従が止まってしまう
        if (!cube || cube->Parent.expired()) {
            if (cube) cube->actor = nullptr;
            if (it->actor) {
                scene->removeActor(*it->actor);
                it->actor->release();
                it->actor = nullptr;
            }
            // RCBN_LOG("Cleaned up removed cube from Physics: " << (cube ? cube->Name : "Unknown"));
            it = cubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // 1. 未反映の新入りを登録
    for (auto& inst : workspace.pendingInstances) {
        if (inst->IsA("BaseCube")) {
            auto cube = std::static_pointer_cast<BaseCube>(inst);
            // 削除済み/別Workspaceへ移動済みのキューブはアクターを作らない。
            // pendingInstances には残留しうる(removeCubeはここを掃除しない)ため、
            // ここで弾かないと死んだキューブにアクターが生成され二重releaseでクラッシュする。
            if (cube->findFirstAncestorWorkspace() != static_cast<Instance*>(&workspace)) continue;
            createActor(cube);
            cubes.push_back({ std::weak_ptr<BaseCube>(cube), cube->actor });
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
    // 衝突が復活しないため、除去後に applyNoCollisionFilterBit で再適用する
    {
        std::vector<std::shared_ptr<BaseCube>> survivors;
        size_t beforeSize = m_noCollisionEntries.size();
        m_noCollisionEntries.erase(
            std::remove_if(m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
                [&](const NoCollisionEntry& e) {
                    if (!e.inst.expired() && !e.c0.expired() && !e.c1.expired()) return false;
                    if (auto c0 = e.c0.lock()) survivors.push_back(c0);
                    if (auto c1 = e.c1.lock()) survivors.push_back(c1);
                    return true;
                }),
            m_noCollisionEntries.end());
        if (m_noCollisionEntries.size() != beforeSize) {
            rebuildNoCollisionPairSet();
            for (auto& c : survivors) applyNoCollisionFilterBit(c);
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
            m_constraints.push_back({std::weak_ptr<Instance>(weld), nullptr});
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

    syncAllCubes();
}

void Physics::removeInvalidConstraints(Workspace& workspace) {
    auto cubeIsInWorkspace = [&workspace](const std::weak_ptr<BaseCube>& cube) {
        auto locked = cube.lock();
        return locked && locked->findFirstAncestorWorkspace() == &workspace;
    };
    auto isInvalid = [&](const std::shared_ptr<Instance>& constraint) {
        if (!constraint) return false;
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
        RCBN_LOG("Removing constraint \"" << constraint->Name << "\" because one of its cubes left the Workspace");
        removeConstraint(constraint);
        if (auto parent = constraint->Parent.lock()) parent->removeChild(constraint->Name);
    }
    workspace.pendingConstraints.erase(
        std::remove_if(workspace.pendingConstraints.begin(), workspace.pendingConstraints.end(),
            [&stale](const std::shared_ptr<Instance>& constraint) {
                return std::binary_search(stale.begin(), stale.end(), constraint);
            }),
        workspace.pendingConstraints.end());
}

void Physics::syncWeldKinematics() {
    // アンカー駆動部(Head等)を先に即時 setGlobalPose で動かし(syncPhysics内でm_weldKinematic
    // のためsetGlobalPoseになる)、その後で非アンカーのメンバー(帽子のCube)のcframeを
    // compoundから読み戻す。2パスに分けるのは、駆動部を動かしてからメンバーを読む必要があるため。
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (cube && cube->m_weldKinematic && cube->Anchored) cube->syncPhysics();
    }
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (cube && cube->m_weldKinematic && !cube->Anchored) cube->syncPhysics();
    }
}

void Physics::moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame) {
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

    if (!member->actor) return;

    // Weld compoundではactor原点とHandleの姿勢が異なるため、Handleのlocal offsetを逆に
    // 適用して共有actorを移動する。これで全シェイプが剛体として手元へ追従する。
    physx::PxTransform handleTarget(
        physx::PxVec3(worldCFrame.Position.x, worldCFrame.Position.y, worldCFrame.Position.z),
        physx::PxQuat(worldCFrame.Rotation.x, worldCFrame.Rotation.y,
                      worldCFrame.Rotation.z, worldCFrame.Rotation.w));
    handleTarget = handleTarget.getNormalized();
    const physx::PxTransform localOffset = member->m_compoundLocalOffset.getNormalized();
    const physx::PxTransform actorTarget = handleTarget.transform(localOffset.getInverse());
    member->actor->setGlobalPose(actorTarget);

    // 持っている間は重力・衝突で残った運動量を引き継がない。
    if (auto* dynamic = member->actor->is<physx::PxRigidDynamic>()) {
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

void Physics::createRope(const std::shared_ptr<Rope>& rope) {
    auto c0 = rope->m_cube0.lock();
    auto c1 = rope->m_cube1.lock();
    if (!c0 || !c1) {
        RCBN_WARN("Rope \"" << rope->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    if (!c0->actor || !c1->actor) {
        RCBN_WARN("Rope \"" << rope->Name << "\": actors not ready");
        return;
    }

    physx::PxTransform frame0 = composeAttachmentFrame(c0->m_compoundLocalOffset, rope->m_attachment0, c0.get());
    physx::PxTransform frame1 = composeAttachmentFrame(c1->m_compoundLocalOffset, rope->m_attachment1, c1.get());
    float dist = rope->MaxDistance;
    if (dist <= 0.0f) {
        auto p0 = c0->actor->getGlobalPose().transform(frame0).p;
        auto p1 = c1->actor->getGlobalPose().transform(frame1).p;
        dist = (p1 - p0).magnitude();
    }

    physx::PxDistanceJoint* joint = PxDistanceJointCreate(
        *s_pxPhysics, c0->actor, frame0, c1->actor, frame1
    );
    joint->setMaxDistance(dist);
    joint->setMinDistance(0.0f);
    joint->setDistanceJointFlag(physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
    if (rope->Stiffness > 0.0f) {
        joint->setStiffness(rope->Stiffness);
        joint->setDamping(rope->Damping);
        joint->setDistanceJointFlag(physx::PxDistanceJointFlag::eSPRING_ENABLED, true);
    }
    // 衝突無効化（連結された2体が衝突しないようにする）
    joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);

    rope->m_joint = joint;
    m_constraints.push_back({ std::weak_ptr<Instance>(rope), joint });
    // // RCBN_LOG("Rope \"" << rope->Name << "\" created, maxDistance=" << dist);
}

void Physics::createRod(const std::shared_ptr<Rod>& rod) {
    auto c0 = rod->m_cube0.lock();
    auto c1 = rod->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("Rod \"" << rod->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    if (!c0->actor || !c1->actor) {
        // RCBN_WARN("Rod \"" << rod->Name << "\": actors not ready");
        return;
    }

    physx::PxTransform frame0 = composeAttachmentFrame(c0->m_compoundLocalOffset, rod->m_attachment0, c0.get());
    physx::PxTransform frame1 = composeAttachmentFrame(c1->m_compoundLocalOffset, rod->m_attachment1, c1.get());
    auto p0 = c0->actor->getGlobalPose().transform(frame0).p;
    auto p1 = c1->actor->getGlobalPose().transform(frame1).p;
    float dist = (p1 - p0).magnitude();

    physx::PxDistanceJoint* joint = PxDistanceJointCreate(
        *s_pxPhysics, c0->actor, frame0, c1->actor, frame1
    );
    joint->setMaxDistance(dist);
    joint->setMinDistance(dist);
    joint->setDistanceJointFlag(physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
    joint->setDistanceJointFlag(physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, true);
    joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);

    rod->m_joint = joint;
    m_constraints.push_back({ std::weak_ptr<Instance>(rod), joint });
    RCBN_LOG("Rod \"" << rod->Name << "\" created, distance=" << dist);
}

void Physics::createBallSocket(const std::shared_ptr<BallSocket>& bs) {
    auto c0 = bs->m_cube0.lock();
    auto c1 = bs->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("BallSocket \"" << bs->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    if (!c0->actor || !c1->actor) {
        // RCBN_WARN("BallSocket \"" << bs->Name << "\": actors not ready");
        return;
    }

    physx::PxTransform frame0 = composeAttachmentFrame(c0->m_compoundLocalOffset, bs->m_attachment0, c0.get());
    physx::PxTransform frame1 = composeAttachmentFrame(c1->m_compoundLocalOffset, bs->m_attachment1, c1.get());

    physx::PxSphericalJoint* joint = PxSphericalJointCreate(
        *s_pxPhysics, c0->actor, frame0, c1->actor, frame1
    );
    joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);

    bs->m_joint = joint;
    m_constraints.push_back({ std::weak_ptr<Instance>(bs), joint });
    RCBN_LOG("BallSocket \"" << bs->Name << "\" created");
}

void Physics::rebuildNoCollisionPairSet() {
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

bool Physics::isInNoCollisionPair(const BaseCube* cube) const {
    if (!cube) return false;
    for (auto& entry : m_noCollisionEntries) {
        auto c0 = entry.c0.lock();
        auto c1 = entry.c1.lock();
        if (!c0 || !c1) continue;
        if (c0.get() == cube || c1.get() == cube) return true;
    }
    return false;
}

void Physics::applyNoCollisionFilterBit(const std::shared_ptr<BaseCube>& cube) {
    if (!cube || !cube->actor) return;

    bool shouldHaveBit = isInNoCollisionPair(cube.get());
    bool changed = false;

    for (physx::PxU32 i = 0; i < cube->actor->getNbShapes(); i++) {
        physx::PxShape* shape = nullptr;
        cube->actor->getShapes(&shape, 1, i);
        if (!shape || shape->userData != cube.get()) continue;

        physx::PxFilterData fd = shape->getSimulationFilterData();
        bool hasBit = (fd.word0 & FILTER_WORD0_NOCOLLISION_CANDIDATE) != 0;
        if (hasBit == shouldHaveBit) continue;

        if (shouldHaveBit) fd.word0 |= FILTER_WORD0_NOCOLLISION_CANDIDATE;
        else                fd.word0 &= ~FILTER_WORD0_NOCOLLISION_CANDIDATE;
        shape->setSimulationFilterData(fd);
        changed = true;
    }

    if (changed) {
        scene->resetFiltering(*cube->actor);
        auto* dyn = cube->actor->is<physx::PxRigidDynamic>();
        if (dyn && !(dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
            dyn->wakeUp();
        }
    }
}

void Physics::createNoCollision(const std::shared_ptr<NoCollision>& nc) {
    auto c0 = nc->m_cube0.lock();
    auto c1 = nc->m_cube1.lock();
    if (!c0 || !c1) {
        // RCBN_WARN("NoCollision \"" << nc->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }

    bool alreadyRegistered = std::any_of(m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
        [&](const NoCollisionEntry& e) { return e.inst.lock() == nc; });
    if (!alreadyRegistered) {
        m_noCollisionEntries.push_back({ std::weak_ptr<Instance>(nc), std::weak_ptr<BaseCube>(c0), std::weak_ptr<BaseCube>(c1) });
    }

    rebuildNoCollisionPairSet();
    applyNoCollisionFilterBit(c0);
    applyNoCollisionFilterBit(c1);
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
        auto verts = cube->getConvexVertices();
        if (verts.empty()) return false;
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

void Physics::rebuildGroup(const std::vector<std::shared_ptr<BaseCube>>& assembly) {
    if (assembly.empty()) return;

    physx::PxVec3 savedLinearVelocity(0.0f);
    physx::PxVec3 savedAngularVelocity(0.0f);
    bool restoreDynamicVelocity = false;
    for (auto& cube : assembly) {
        auto* dynamic = cube->actor ? cube->actor->is<physx::PxRigidDynamic>() : nullptr;
        if (!dynamic || (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) continue;
        savedLinearVelocity = dynamic->getLinearVelocity();
        savedAngularVelocity = dynamic->getAngularVelocity();
        restoreDynamicVelocity = true;
        break;
    }

    // 1. アクター破棄前にワールド姿勢を保存
    std::unordered_map<BaseCube*, physx::PxTransform> savedPoses;
    for (auto& cube : assembly) {
        if (cube->actor) {
            // PxTransform::getInverse()/transform() は単位Quaternionを前提とする。
            // compoundの再構築を重ねても丸め誤差が位置の拡大へ変換されないよう、合成前後で正規化する。
            const physx::PxTransform actorPose = cube->actor->getGlobalPose().getNormalized();
            const physx::PxTransform localOffset = cube->m_compoundLocalOffset.getNormalized();
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

    // 2. 既存アクターを破棄（compound の重複解放を防ぐため set で管理）
    std::unordered_set<physx::PxRigidActor*> toRelease;
    for (auto& cube : assembly) {
        if (cube->actor) toRelease.insert(cube->actor);
    }
    for (auto* a : toRelease) {
        scene->removeActor(*a);
        a->release();
    }

    // 3. cube の actor/offset をリセット
    std::unordered_set<BaseCube*> assemblyPtrs;
    for (auto& cube : assembly) {
        cube->actor = nullptr;
        cube->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity);
        assemblyPtrs.insert(cube.get());
    }
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (cube && assemblyPtrs.count(cube.get())) entry.actor = nullptr;
    }

    // 4. compound 生成（assembly[0] を原点）
    physx::PxTransform originPose = savedPoses[assembly[0].get()].getNormalized();
    physx::PxRigidDynamic* compound = s_pxPhysics->createRigidDynamic(originPose);
    compound->setSolverIterationCounts(8, 2);

    // シェイプが実際に追加された順に密度を記録し、updateMassAndInertia の
    // per-shape density 配列（getShapes() の並び = attachShape した順）に対応させる
    std::vector<physx::PxReal> shapeDensities;
    for (size_t i = 0; i < assembly.size(); i++) {
        auto& cube = assembly[i];
        physx::PxTransform localOffset = (i == 0)
            ? physx::PxTransform(physx::PxIdentity)
            : originPose.getInverse().transform(savedPoses[cube.get()]).getNormalized();
        physx::PxMaterial* mat = getOrCreateMaterial(cube->material);
        if (attachShapeToCompound(s_pxPhysics, cube, compound, localOffset, mat)) {
            shapeDensities.push_back(std::max(cube->MassDensity, 0.01f));
        }
        cube->actor = compound;
        cube->m_compoundLocalOffset = localOffset;
    }

    // アンカー付きキューブが含まれる場合は compound をキネマティックに設定
    bool anyAnchored = false;
    for (auto& cube : assembly) {
        if (cube->Anchored) { anyAnchored = true; break; }
    }
    if (anyAnchored) {
        // キネマティックボディはCCD非対応のため、CCDは有効化しない（PhysX警告回避）
        compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
    } else {
        compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
        // createActor()と同様にLockFlagsを反映する(OR合成)。これをしないと、Humanoid.Rootのように
        // 角度ロックを持つキューブがWeld等でcompound化されるたびにロックが失われ、
        // 着席/離脱の繰り返しなどでキャラクターが転倒しやすくなる
        physx::PxRigidDynamicLockFlags combinedLockFlags = (physx::PxRigidDynamicLockFlags)0;
        for (auto& cube : assembly) combinedLockFlags |= cube->LockFlags;
        compound->setRigidDynamicLockFlags(combinedLockFlags);
    }

    // 質量計算はシェイプが1つ以上ある場合のみ行う。CanCollide==false のキューブだけで
    // 構成されたアセンブリ(例: 装飾用 MeshCube を Head に Weld した髪)はシェイプを持たず、
    // ゼロシェイプの body に対して updateMassAndInertia を呼ぶと不正なメモリアクセス
    // (0x0 実行)でクラッシュするため、固定質量・慣性で代替する（位置追従のみで十分）。
    // 各パーツの BaseCube::MassDensity を per-shape density として渡すことで、
    // 単独アクター(createActor)と同じ基準でパーツごとの密度を質量に反映する。
    if (compound->getNbShapes() > 0) {
        bool ok = physx::PxRigidBodyExt::updateMassAndInertia(
            *compound, shapeDensities.data(), static_cast<physx::PxU32>(shapeDensities.size()));
        if (!ok) {
            // 失敗時（縮退ジオメトリ等）は質量ゼロ/不正な慣性のcompoundをシーンに残さないよう固定値で保険をかける
            RCBN_WARN("updateMassAndInertia (per-shape) failed for compound — falling back to mass=1.0");
            compound->setMass(1.0f);
            compound->setMassSpaceInertiaTensor(physx::PxVec3(1.0f, 1.0f, 1.0f));
        }
    } else {
        compound->setMass(1.0f);
        compound->setMassSpaceInertiaTensor(physx::PxVec3(1.0f, 1.0f, 1.0f));
    }

    // メンバーに「アンカー駆動のキネマティックWeldか」を記録する。
    // (syncPhysics()/syncWeldKinematics() が setGlobalPose で即時追従させるための判定用)
    for (auto& cube : assembly) {
        cube->m_weldKinematic = anyAnchored;
    }

    if (restoreDynamicVelocity && !anyAnchored) {
        compound->setLinearVelocity(savedLinearVelocity);
        compound->setAngularVelocity(savedAngularVelocity);
    }

    scene->addActor(*compound);
    // createActor()と同様にuserDataを設定する。これが無いとRCBNContactCallback::onContact
    // (Touched通知)やレイキャストがcompound化されたキューブ(Weld/Motor等)を解決できず、
    // 素通りしてしまう。代表としてassembly[0](compoundの原点キューブ)を指す
    compound->userData = assembly[0].get();

    // NoCollision の対象キューブが含まれていれば、対応するシェイプに候補ビットを立てる
    for (physx::PxU32 i = 0; i < compound->getNbShapes(); i++) {
        physx::PxShape* shape = nullptr;
        compound->getShapes(&shape, 1, i);
        if (!shape || !shape->userData) continue;
        auto* shapeCube = static_cast<BaseCube*>(shape->userData);
        if (!isInNoCollisionPair(shapeCube)) continue;
        physx::PxFilterData fd = shape->getSimulationFilterData();
        fd.word0 |= FILTER_WORD0_NOCOLLISION_CANDIDATE;
        shape->setSimulationFilterData(fd);
    }

    // 5. cubes エントリーを更新（既存エントリのみ更新し、未登録のcubeは新規追加する。
    // 未登録のまま残すと、このcubeのactorはcompound解放後も誰にもnullされず、
    // BaseCubeの実デストラクタでdangling actorへアクセスしてクラッシュする）
    for (auto& cube : assembly) {
        bool found = false;
        for (auto& entry : cubes) {
            auto c = entry.cube.lock();
            if (c && c.get() == cube.get()) { entry.actor = compound; found = true; break; }
        }
        if (!found) {
            cubes.push_back({ std::weak_ptr<BaseCube>(cube), compound });
        }
    }

    // 6. m_constraints の Weld で、両端が assembly 内にある Weld の m_compound を更新
    for (auto& cEntry : m_constraints) {
        auto inst = cEntry.constraint.lock();
        if (!inst || !inst->IsA("Weld")) continue;
        auto ew = std::static_pointer_cast<Weld>(inst);
        auto ec0 = ew->m_cube0.lock();
        auto ec1 = ew->m_cube1.lock();
        if (ec0 && ec1 && assemblyPtrs.count(ec0.get()) && assemblyPtrs.count(ec1.get())) {
            ew->m_compound = compound;
        }
    }

    // 7. assembly 内の cube を参照している Rope/Rod/Motor を再構築
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
            auto r = std::static_pointer_cast<Rope>(inst);
            r->m_joint = nullptr; createRope(r);
        } else if (inst->IsA("Rod")) {
            auto r = std::static_pointer_cast<Rod>(inst);
            r->m_joint = nullptr; createRod(r);
        } else if (inst->IsA("Motor")) {
            auto m = std::static_pointer_cast<Motor>(inst);
            m->m_joint = nullptr; createMotor(m);
        } else if (inst->IsA("BallSocket")) {
            auto b = std::static_pointer_cast<BallSocket>(inst);
            b->m_joint = nullptr; createBallSocket(b);
        }
    }
}

void Physics::createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace) {
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

    // この Weld の m_compound を設定
    weld->m_compound = static_cast<physx::PxRigidDynamic*>(assembly[0]->actor);

    // m_constraints に未登録なら追加
    bool alreadyRegistered = std::any_of(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& e) { return e.constraint.lock() == weld; });
    if (!alreadyRegistered) {
        m_constraints.push_back({ std::weak_ptr<Instance>(weld), nullptr });
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

void Physics::createMotor(const std::shared_ptr<Motor>& motor) {
    auto c0 = motor->m_cube0.lock();
    auto c1 = motor->m_cube1.lock();
    if (!c0 || !c1) {
        RCBN_WARN("Motor \"" << motor->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    if (!c0->actor || !c1->actor) {
        RCBN_WARN("Motor \"" << motor->Name << "\": actors not ready");
        return;
    }

    // ピボット: 各キューブ自身の CFrame から midpoint を使う
    // (Weld コンパウンドに取り込まれた場合でも actor pose ではなく個別座標が正しい)
    // Attachment0/Attachment1 が設定されていればその位置をピボットに使う:
    //   両方設定 → 各キューブ側のピボットをそれぞれの Attachment 位置に
    //   片方のみ → その Attachment 位置を共有ピボットに
    //   未設定   → 従来通り 2 キューブの中点
    physx::PxTransform pose0 = c0->actor->getGlobalPose();
    physx::PxTransform pose1 = c1->actor->getGlobalPose();
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

    physx::PxRevoluteJoint* joint = PxRevoluteJointCreate(
        *s_pxPhysics, c0->actor, frame0, c1->actor, frame1
    );
    if (!joint) {
        RCBN_WARN("Motor \"" << motor->Name << "\": PxRevoluteJointCreate failed");
        return;
    }
    joint->setRevoluteJointFlag(physx::PxRevoluteJointFlag::eDRIVE_ENABLED, true);
    joint->setDriveVelocity(motor->DriveVelocity);
    joint->setDriveForceLimit(motor->MaxForce);
    // 連結体同士の衝突を無効化（接触面で詰まらないように）
    joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);

    motor->m_joint = joint;
    m_constraints.push_back({ std::weak_ptr<Instance>(motor), joint });
    RCBN_LOG("Motor \"" << motor->Name << "\" created at pivot (" << pivotWorld.x << ", " << pivotWorld.y << ", " << pivotWorld.z << ")");
}

void Physics::removeConstraint(const std::shared_ptr<Instance>& c) {
    if (c->IsA("NoCollision")) {
        auto ncIt = std::find_if(m_noCollisionEntries.begin(), m_noCollisionEntries.end(),
            [&](const NoCollisionEntry& e) { return e.inst.lock() == c; });
        if (ncIt == m_noCollisionEntries.end()) return;

        auto oldC0 = ncIt->c0.lock();
        auto oldC1 = ncIt->c1.lock();
        m_noCollisionEntries.erase(ncIt);
        rebuildNoCollisionPairSet();
        if (oldC0) applyNoCollisionFilterBit(oldC0);
        if (oldC1) applyNoCollisionFilterBit(oldC1);
        return;
    }

    auto it = std::find_if(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& e) { return e.constraint.lock() == c; });
    if (it == m_constraints.end()) return;

    if (c->IsA("Weld")) {
        auto weld = std::static_pointer_cast<Weld>(c);

        // weld->m_compound はキャッシュした生ポインタであり、別経路(他Weldの削除に伴う
        // 再構築や Workspace 破棄)で既に release 済み = dangling になっている場合がある。
        // dangling ポインタは nullptr ではないので nullptr チェックでは弾けない。
        // 唯一信頼できる「生存判定」は cubes テーブル: compound が解放されるとき必ず
        // 対応する cube->actor / entry.actor は nullptr へ更新されるため、いずれかの
        // 生存 cube が今も m_compound を指しているかどうかで判定する。
        physx::PxRigidDynamic* oldCompound = weld->m_compound;

        // 1. 旧 compound を共有していた全キューブを収集（＝生存判定も兼ねる）
        std::vector<std::shared_ptr<BaseCube>> oldGroupCubes;
        if (oldCompound) {
            for (auto& entry : cubes) {
                auto cube = entry.cube.lock();
                if (cube && cube->actor == oldCompound)
                    oldGroupCubes.push_back(cube);
            }
        }

        // どの生存 cube も指していない → compound は既に解放済み(dangling) または未構築。
        // この場合 actor には一切触れず、参照をクリアして Weld を除去するだけにする。
        if (oldGroupCubes.empty()) {
            for (auto& cEntry : m_constraints) {
                auto inst = cEntry.constraint.lock();
                if (inst && inst->IsA("Weld")) {
                    auto ew = std::static_pointer_cast<Weld>(inst);
                    if (oldCompound && ew->m_compound == oldCompound) ew->m_compound = nullptr;
                }
            }
            weld->m_compound = nullptr;
            m_constraints.erase(it);
            return;
        }

        {
            // 旧 compound を破棄（ここに来た時点で生存が確認できている）
            scene->removeActor(*oldCompound);
            oldCompound->release();

            // 全キューブの actor/offset をリセット
            std::unordered_set<BaseCube*> oldPtrs;
            for (auto& cube : oldGroupCubes) {
                cube->actor = nullptr;
                cube->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity);
                oldPtrs.insert(cube.get());
            }
            for (auto& entry : cubes) {
                auto cube = entry.cube.lock();
                if (cube && oldPtrs.count(cube.get())) entry.actor = nullptr;
            }
            // この compound を参照していた全 Weld の m_compound をクリア
            for (auto& cEntry : m_constraints) {
                auto inst = cEntry.constraint.lock();
                if (inst && inst->IsA("Weld")) {
                    auto ew = std::static_pointer_cast<Weld>(inst);
                    if (ew->m_compound == oldCompound) ew->m_compound = nullptr;
                }
            }
            weld->m_compound = nullptr;
        }

        // 2. この Weld を m_constraints から削除（BFS の前に除外する）
        m_constraints.erase(it);

        // 3. 旧グループを残存 Weld で連結成分に分割し、各成分を再構築
        if (!oldGroupCubes.empty()) {
            // Workspace から既に除去されたキューブ(cubes に未登録)は BFS でグループへ
            // 引き戻さない。削除カスケード中に removeCube 済みのキューブへここで actor を
            // 再代入してしまうと、cubes にエントリが無いため後続の removeCube の共有判定
            // から漏れて compound が release され、そのキューブのデストラクタが
            // 解放済み actor に対して release() を呼びアクセス違反になる
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
                        if (entry.cube.lock() == subGroup[0]) entry.actor = subGroup[0]->actor;
                    }
                } else {
                    rebuildGroup(subGroup);
                }
            }
        }
        return;
    }

    // Weld 以外: joint を解放してエントリーを削除
    if (it->joint) it->joint->release();
    m_constraints.erase(it);
}
