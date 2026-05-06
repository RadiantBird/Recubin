#include "include/Core/Physics.hpp"
#include "include/Util/Logger.hpp"
#include <include/PhysX/cooking/PxCooking.h>
#include <unordered_set>

void Physics::init() {
    foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
    physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, physx::PxTolerancesScale());
    
    physx::PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f); // 重力設定
    sceneDesc.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(1);
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
    scene = physics->createScene(sceneDesc);
    PxInitExtensions(*physics, nullptr);
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

        // Weld compound アクターのリリース（cube->actor が compound を指しているため
        // cubes ループで重複リリースしないよう、リリース済みポインタを nullptr にする）
        std::unordered_set<physx::PxRigidActor*> released;
        for (auto& entry : cubes) {
            if (entry.actor && released.find(entry.actor) == released.end()) {
                scene->removeActor(*entry.actor);
                entry.actor->release();
                released.insert(entry.actor);
            }
            entry.actor = nullptr;
        }
        cubes.clear();
        scene->release();
        scene = nullptr;
    }
    PxCloseExtensions();
    if (physics) {
        physics->release();
        physics = nullptr;
    }
    if (foundation) {
        foundation->release();
        foundation = nullptr;
    }
}

void Physics::createActor(const std::shared_ptr<BaseCube>& cube) {
    if (!cube->CanCollide) return; // 衝突無効 → actor 不要
    if (cube->actor) return; // 二重登録防止

    // 初期姿勢
    physx::PxTransform transform(
        physx::PxVec3(cube->cframe.Position.x, cube->cframe.Position.y, cube->cframe.Position.z),
        physx::PxQuat(cube->cframe.Rotation.x, cube->cframe.Rotation.y, cube->cframe.Rotation.z, cube->cframe.Rotation.w)
    );

    physx::PxRigidActor* actor = nullptr;
    if (cube->Anchored) {
        physx::PxRigidDynamic* kin = physics->createRigidDynamic(transform);
        kin->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
        actor = kin;
    } else {
        physx::PxRigidDynamic* dynamicActor = physics->createRigidDynamic(transform);
        dynamicActor->setRigidDynamicLockFlags(cube->LockFlags);
        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
        dynamicActor->setSolverIterationCounts(8, 2);
        actor = dynamicActor;
    }

    physx::PxMaterial* pxMat = getOrCreateMaterial(cube->material);

    switch (cube->getPhysicsShape()) {
    case PhysicsShape::Box: {
        physx::PxBoxGeometry geom(cube->Size.x/2, cube->Size.y/2, cube->Size.z/2);
        physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
        break;
    }
    case PhysicsShape::Sphere: {
        physx::PxSphereGeometry geom(cube->Size.x / 2.f);
        physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
        break;
    }
    case PhysicsShape::ConvexMesh: {
        auto verts = cube->getConvexVertices();
        physx::PxCookingParams cookParams(physics->getTolerancesScale());
        physx::PxConvexMeshDesc desc;
        desc.points.count  = static_cast<physx::PxU32>(verts.size());
        desc.points.stride = sizeof(physx::PxVec3);
        desc.points.data   = verts.data();
        desc.flags         = physx::PxConvexFlag::eCOMPUTE_CONVEX;
        physx::PxDefaultMemoryOutputStream buf;
        physx::PxConvexMeshCookingResult::Enum result;
        if (!PxCookConvexMesh(cookParams, desc, buf, &result)) {
            RCBN_WARN("ConvexMesh cooking failed for: " << cube->Name);
            actor->release();
            return;
        }
        physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
        physx::PxConvexMesh* mesh = physics->createConvexMesh(input);
        physx::PxMeshScale scale(physx::PxVec3(cube->Size.x, cube->Size.y, cube->Size.z));
        physx::PxConvexMeshGeometry geom(mesh, scale);
        physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *pxMat);
        mesh->release();
        break;
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
    if (materialCache.count(m.type)) {
        return materialCache[m.type];
    }

    physx::PxMaterial* pxMat = physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
    materialCache[m.type] = pxMat;
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
    if (cube->actor) {
        scene->removeActor(*cube->actor);
        cube->actor->release();
        cube->actor = nullptr;
    }
    createActor(cube);
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

    // 0. 削除されたキューブをクリーンアップ（Workspace に存在しなくなったキューブを検出）
    auto it = cubes.begin();
    while (it != cubes.end()) {
        auto cube = it->cube.lock();
        // オブジェクトが消滅しているか、actor が nullptr か、Workspace の子孫でなくなった場合は削除
        if (!cube || !it->actor || cube->Parent.expired()) {
            if (it->actor) {
                scene->removeActor(*it->actor);
                it->actor->release();
                it->actor = nullptr;
            }
            RCBN_LOG("Cleaned up removed cube from Physics: " << (cube ? cube->Name : "Unknown"));
            it = cubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // 1. 未反映の新入りを登録
    for (auto& inst : workspace.pendingInstances) {
        if (inst->IsA("BaseCube")) {
            auto cube = std::static_pointer_cast<BaseCube>(inst);
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

    // 3. 制約の新規登録
    for (auto& c : workspace.pendingConstraints) {
        if (c->IsA("Rope"))       createRope(std::static_pointer_cast<Rope>(c));
        else if (c->IsA("Rod"))   createRod(std::static_pointer_cast<Rod>(c));
        else if (c->IsA("Weld"))  createWeld(std::static_pointer_cast<Weld>(c));
        else if (c->IsA("Motor")) createMotor(std::static_pointer_cast<Motor>(c));
    }
    workspace.pendingConstraints.clear();

    for (auto& entry : cubes) {
        if (auto cube = entry.cube.lock()) {
            cube->syncPhysics();
        }
    }
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

    physx::PxTransform frame(physx::PxIdentity);
    float dist = rope->MaxDistance;
    if (dist <= 0.0f) {
        auto p0 = c0->actor->getGlobalPose().p;
        auto p1 = c1->actor->getGlobalPose().p;
        dist = (p1 - p0).magnitude();
    }

    physx::PxDistanceJoint* joint = PxDistanceJointCreate(
        *physics, c0->actor, frame, c1->actor, frame
    );
    joint->setMaxDistance(dist);
    joint->setMinDistance(0.0f);
    joint->setDistanceJointFlag(physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
    // 衝突無効化（連結された2体が衝突しないようにする）
    joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);

    rope->m_joint = joint;
    m_constraints.push_back({ std::weak_ptr<Instance>(rope), joint });
    RCBN_LOG("Rope \"" << rope->Name << "\" created, maxDistance=" << dist);
}

void Physics::createRod(const std::shared_ptr<Rod>& rod) {
    auto c0 = rod->m_cube0.lock();
    auto c1 = rod->m_cube1.lock();
    if (!c0 || !c1) {
        RCBN_WARN("Rod \"" << rod->Name << "\": cube refs unresolved (c0=" << (c0?"ok":"null") << ", c1=" << (c1?"ok":"null") << ")");
        return;
    }
    if (!c0->actor || !c1->actor) {
        RCBN_WARN("Rod \"" << rod->Name << "\": actors not ready");
        return;
    }

    auto p0 = c0->actor->getGlobalPose().p;
    auto p1 = c1->actor->getGlobalPose().p;
    float dist = (p1 - p0).magnitude();

    physx::PxTransform frame(physx::PxIdentity);
    physx::PxDistanceJoint* joint = PxDistanceJointCreate(
        *physics, c0->actor, frame, c1->actor, frame
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

// Weld 用シェイプ追加ヘルパー
static void attachShapeToCompound(
    physx::PxPhysics* physics,
    const std::shared_ptr<BaseCube>& cube,
    physx::PxRigidDynamic* compound,
    const physx::PxTransform& localOffset,
    physx::PxMaterial* mat)
{
    switch (cube->getPhysicsShape()) {
    case PhysicsShape::Box: {
        physx::PxBoxGeometry geom(cube->Size.x / 2, cube->Size.y / 2, cube->Size.z / 2);
        physx::PxShape* shape = physics->createShape(geom, *mat);
        shape->setLocalPose(localOffset);
        compound->attachShape(*shape);
        shape->release();
        break;
    }
    case PhysicsShape::Sphere: {
        physx::PxSphereGeometry geom(cube->Size.x / 2.0f);
        physx::PxShape* shape = physics->createShape(geom, *mat);
        shape->setLocalPose(localOffset);
        compound->attachShape(*shape);
        shape->release();
        break;
    }
    case PhysicsShape::ConvexMesh:
        RCBN_WARN("Weld: ConvexMesh shape in compound not supported yet, skipping");
        break;
    }
}

void Physics::createWeld(const std::shared_ptr<Weld>& weld) {
    auto c0 = weld->m_cube0.lock();
    auto c1 = weld->m_cube1.lock();
    if (!c0 || !c1) {
        RCBN_WARN("Weld \"" << weld->Name << "\": cube refs unresolved");
        return;
    }
    if (!c0->actor || !c1->actor) {
        RCBN_WARN("Weld \"" << weld->Name << "\": actors not ready");
        return;
    }

    // 連鎖 Weld は未対応：すでに compound に組み込まれているキューブをチェック
    for (auto& entry : m_constraints) {
        auto existing = entry.constraint.lock();
        if (!existing || !existing->IsA("Weld")) continue;
        auto ew = std::static_pointer_cast<Weld>(existing);
        if (ew->m_cube0.lock() == c0 || ew->m_cube1.lock() == c0
         || ew->m_cube0.lock() == c1 || ew->m_cube1.lock() == c1) {
            RCBN_WARN("Weld \"" << weld->Name << "\": chained welds not supported, skipping");
            return;
        }
    }

    physx::PxTransform pose0 = c0->actor->getGlobalPose();
    physx::PxTransform pose1 = c1->actor->getGlobalPose();

    // cube1 の compound 内ローカルオフセット = pose0^-1 * pose1
    physx::PxTransform offset1 = pose0.getInverse().transform(pose1);

    // 既存アクターをシーンから除去（cubes エントリーの actor は nullptr にしてデストラクタで二重解放を防ぐ）
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (cube == c0 || cube == c1) {
            if (entry.actor) {
                scene->removeActor(*entry.actor);
                entry.actor->release();
                entry.actor = nullptr;
            }
        }
    }
    c0->actor = nullptr;
    c1->actor = nullptr;

    // compound アクター生成（cube0 の姿勢を基準）
    physx::PxRigidDynamic* compound = physics->createRigidDynamic(pose0);
    compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
    compound->setSolverIterationCounts(8, 2);

    physx::PxMaterial* mat = getOrCreateMaterial(c0->material);
    attachShapeToCompound(physics, c0, compound, physx::PxTransform(physx::PxIdentity), mat);
    attachShapeToCompound(physics, c1, compound, offset1, mat);

    physx::PxRigidBodyExt::updateMassAndInertia(*compound, 1.0f);
    scene->addActor(*compound);

    c0->actor = compound;
    c1->actor = compound;
    c0->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity);
    c1->m_compoundLocalOffset = offset1;

    // cubes エントリーを更新して compound を指すようにする
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (cube == c0 || cube == c1) {
            entry.actor = compound;
        }
    }

    weld->m_compound = compound;
    // Weld は joint を持たないので joint = nullptr
    m_constraints.push_back({ std::weak_ptr<Instance>(weld), nullptr });
    RCBN_LOG("Weld \"" << weld->Name << "\" compound created (cubes: " << c0->Name << ", " << c1->Name << ")");
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

    // 接着面の中心（ピボット）を計算: cube0 の面のうち cube1 に最も近い面
    physx::PxTransform pose0 = c0->actor->getGlobalPose();
    physx::PxTransform pose1 = c1->actor->getGlobalPose();
    physx::PxVec3 toTarget = pose1.p - pose0.p;

    physx::PxVec3 faces[6] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
    };
    float bestDot = -FLT_MAX;
    physx::PxVec3 bestHalf(c0->Size.x / 2, 0, 0);
    for (int i = 0; i < 6; i++) {
        physx::PxVec3 worldDir = pose0.rotate(faces[i]);
        float d = worldDir.dot(toTarget);
        if (d > bestDot) {
            bestDot = d;
            bestHalf = physx::PxVec3(
                faces[i].x * c0->Size.x / 2,
                faces[i].y * c0->Size.y / 2,
                faces[i].z * c0->Size.z / 2
            );
        }
    }
    physx::PxVec3 pivotWorld = pose0.p + pose0.rotate(bestHalf);

    // 回転軸を cube0 ローカルの X 軸として表現するフレームを構築
    physx::PxVec3 axisW(motor->Axis.x, motor->Axis.y, motor->Axis.z);
    axisW.normalize();
    physx::PxQuat axisRot = computeShortestRotationFromX(axisW);

    physx::PxTransform frame0 = pose0.transformInv(physx::PxTransform(pivotWorld, axisRot));
    physx::PxTransform frame1 = pose1.transformInv(physx::PxTransform(pivotWorld, axisRot));

    physx::PxRevoluteJoint* joint = PxRevoluteJointCreate(
        *physics, c0->actor, frame0, c1->actor, frame1
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
    auto it = std::find_if(m_constraints.begin(), m_constraints.end(),
        [&](const ConstraintEntry& e) { return e.constraint.lock() == c; });
    if (it == m_constraints.end()) return;

    if (c->IsA("Weld")) {
        auto weld = std::static_pointer_cast<Weld>(c);
        if (weld->m_compound) {
            // compound を解体: cube のアクターポインタと cubes エントリーをリセット
            auto c0 = weld->m_cube0.lock();
            auto c1 = weld->m_cube1.lock();
            for (auto& entry : cubes) {
                auto cube = entry.cube.lock();
                if (cube == c0 || cube == c1) {
                    entry.actor = nullptr; // 解放はここでは行わない
                }
            }
            scene->removeActor(*weld->m_compound);
            weld->m_compound->release();
            weld->m_compound = nullptr;
            if (c0) { c0->actor = nullptr; c0->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity); }
            if (c1) { c1->actor = nullptr; c1->m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity); }
        }
    } else if (it->joint) {
        it->joint->release();
    }

    m_constraints.erase(it);
}