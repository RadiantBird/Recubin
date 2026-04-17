#include "include/Core/Physics.hpp"
#include "include/Util/Logger.hpp"

void Physics::init() {
    foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
    physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, physx::PxTolerancesScale());
    
    physx::PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f); // 重力設定
    sceneDesc.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(1);
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    scene = physics->createScene(sceneDesc);

    defaultMaterial = physics->createMaterial(0.5f, 0.5f, 0.6f); // 摩擦, 反発
}

void Physics::createActor(BaseCube* cube) {
    // 形状(Half-extentsなので半分にする)
    physx::PxBoxGeometry geometry(cube->Size.x/2, cube->Size.y/2, cube->Size.z/2);
    
    // 初期姿勢
    physx::PxTransform transform(
        physx::PxVec3(cube->cframe.Position.x, cube->cframe.Position.y, cube->cframe.Position.z),
        physx::PxQuat(cube->cframe.Rotation.x, cube->cframe.Rotation.y, cube->cframe.Rotation.z, cube->cframe.Rotation.w)
    );

    physx::PxRigidActor* actor = nullptr;
    if (cube->Anchored) {
        actor = physics->createRigidStatic(transform);
    } else {
        physx::PxRigidDynamic* dynamicActor = physics->createRigidDynamic(transform);
        dynamicActor->setRigidDynamicLockFlags(cube->LockFlags);
        actor = dynamicActor;
    }

    // 形状とマテリアルの紐付け
    physx::PxRigidActorExt::createExclusiveShape(*actor, geometry, *defaultMaterial);
    
    scene->addActor(*actor);
    actor->userData = cube; // レイキャスト等で逆引きできるようにポインタを保持
    cube->actor = actor; // BaseCube側に参照を戻す
}

bool Physics::raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, physx::PxRigidActor* ignoreActor) {
    if (!scene) return false;

    physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
    physx::PxVec3 pxDir(direction.x, direction.y, direction.z);
    
    if (pxDir.magnitudeSquared() < 1e-6f) return false;
    pxDir.normalize();

    // 複数のヒットを想定（自分自身を突き抜けるため）
    const physx::PxU32 maxHits = 4;
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
        
        // ブロッキングヒットが無視対象だった場合、次の候補を探す
        if (!bestHit) {
            for (physx::PxU32 i = 0; i < buf.nbTouches; i++) {
                if (buf.touches[i].actor != ignoreActor) {
                    bestHit = &buf.touches[i];
                    break;
                }
            }
        }

        if (bestHit) {
            hitResult.hit = true;
            hitResult.distance = bestHit->distance;
            hitResult.position = Vector3(bestHit->position.x, bestHit->position.y, bestHit->position.z);
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

void Physics::removeCube(BaseCube* cube) {
    if (!cube) return;
    
    // PhysX から削除
    if (cube->actor) {
        try {
            scene->removeActor(*cube->actor);
            cube->actor->release();
        } catch (...) {
            RCBN_WARN("Error removing actor: " << cube->Name);
        }
        cube->actor = nullptr;
    }
    
    // cubes ベクターから削除
    auto it = std::find(cubes.begin(), cubes.end(), cube);
    if (it != cubes.end()) {
        cubes.erase(it);
        RCBN_LOG("Removed cube from Physics: " << cube->Name);
    }
}

void Physics::update(Workspace& workspace, float dt) {
    // d_print("Update Frame Start | dt: " << dt);
    static float accumulator = 0.0f;
    const float fixedStep = 1.0f / 60.0f;
    const int MAX_STEPS = 10;

    // あまりにデカすぎるdt（フリーズ後など）は、ここで切り捨てる
    if (dt > 0.25f) dt = 0.25f; 

    accumulator += dt;

    int steps = 0;
    while (accumulator >= fixedStep) {
        scene->simulate(fixedStep);
        scene->fetchResults(true);

        accumulator -= fixedStep;
        steps++;

        // 安全装置の発動
        if (steps >= MAX_STEPS) {
            accumulator = 0.0f; // 追いつけない分は「なかったこと」にする（スローモーション化）
            RCBN_WARN("Physics safety break engaged! (Spiral of Death prevented)");
            break;
        }
    }
    
    // 0. 削除されたキューブをクリーンアップ（Workspace に存在しなくなったキューブを検出）
    auto it = cubes.begin();
    while (it != cubes.end()) {
        BaseCube* cube = *it;
        // actor が nullptr か、Workspace の子孫でなくなった場合は削除
        if (!cube->actor || !cube->Parent) {
            if (cube->actor) {
                scene->removeActor(*cube->actor);
                cube->actor->release();
                cube->actor = nullptr;
            }
            RCBN_LOG("Cleaned up removed cube from Physics: " << cube->Name);
            it = cubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // 1. 未反映の新入りを登録
    for (Instance* inst : workspace.pendingInstances) {
        if (inst->IsA("BaseCube")) {
            createActor(static_cast<BaseCube*>(inst));
            cubes.push_back(static_cast<BaseCube*>(inst));
        }
    }
    workspace.pendingInstances.clear();

    for (BaseCube* cube : cubes) {
        cube->syncPhysics();
    }
}