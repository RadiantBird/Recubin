#include "include/Core/Physics.hpp"
#include "include/Util/Logger.hpp"
#include <include/PhysX/cooking/PxCooking.h>
#include <unordered_set>
#include <algorithm>
#include <queue>

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

    // 3. 制約の新規登録（Weld を先に処理して compound を確定させてから Rope/Rod/Motor を生成）
    for (auto& c : workspace.pendingConstraints) {
        if (c->IsA("Weld")) createWeld(std::static_pointer_cast<Weld>(c), workspace);
    }
    for (auto& c : workspace.pendingConstraints) {
        if      (c->IsA("Rope"))  createRope(std::static_pointer_cast<Rope>(c));
        else if (c->IsA("Rod"))   createRod(std::static_pointer_cast<Rod>(c));
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

    physx::PxTransform frame0 = c0->m_compoundLocalOffset;
    physx::PxTransform frame1 = c1->m_compoundLocalOffset;
    float dist = rope->MaxDistance;
    if (dist <= 0.0f) {
        auto p0 = c0->actor->getGlobalPose().transform(frame0).p;
        auto p1 = c1->actor->getGlobalPose().transform(frame1).p;
        dist = (p1 - p0).magnitude();
    }

    physx::PxDistanceJoint* joint = PxDistanceJointCreate(
        *physics, c0->actor, frame0, c1->actor, frame1
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

    physx::PxTransform frame0 = c0->m_compoundLocalOffset;
    physx::PxTransform frame1 = c1->m_compoundLocalOffset;
    auto p0 = c0->actor->getGlobalPose().transform(frame0).p;
    auto p1 = c1->actor->getGlobalPose().transform(frame1).p;
    float dist = (p1 - p0).magnitude();

    physx::PxDistanceJoint* joint = PxDistanceJointCreate(
        *physics, c0->actor, frame0, c1->actor, frame1
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

void Physics::rebuildGroup(const std::vector<std::shared_ptr<BaseCube>>& assembly) {
    if (assembly.empty()) return;

    // 1. アクター破棄前にワールド姿勢を保存
    std::unordered_map<BaseCube*, physx::PxTransform> savedPoses;
    for (auto& cube : assembly) {
        if (cube->actor) {
            savedPoses[cube.get()] = cube->actor->getGlobalPose().transform(cube->m_compoundLocalOffset);
        } else {
            auto wp = cube->getWorldPosition();
            auto wr = cube->getWorldCFrame().Rotation;
            savedPoses[cube.get()] = physx::PxTransform(
                physx::PxVec3(wp.x, wp.y, wp.z),
                physx::PxQuat(wr.x, wr.y, wr.z, wr.w)
            );
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
    physx::PxTransform originPose = savedPoses[assembly[0].get()];
    physx::PxRigidDynamic* compound = physics->createRigidDynamic(originPose);
    compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
    compound->setSolverIterationCounts(8, 2);

    for (size_t i = 0; i < assembly.size(); i++) {
        auto& cube = assembly[i];
        physx::PxTransform localOffset = (i == 0)
            ? physx::PxTransform(physx::PxIdentity)
            : originPose.getInverse().transform(savedPoses[cube.get()]);
        physx::PxMaterial* mat = getOrCreateMaterial(cube->material);
        attachShapeToCompound(physics, cube, compound, localOffset, mat);
        cube->actor = compound;
        cube->m_compoundLocalOffset = localOffset;
    }

    physx::PxRigidBodyExt::updateMassAndInertia(*compound, 1.0f);

    // アンカー付きキューブが含まれる場合は compound をキネマティックに設定
    bool anyAnchored = false;
    for (auto& cube : assembly) {
        if (cube->Anchored) { anyAnchored = true; break; }
    }
    if (anyAnchored) {
        compound->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
    }

    scene->addActor(*compound);

    // 5. cubes エントリーを更新
    for (auto& entry : cubes) {
        auto cube = entry.cube.lock();
        if (cube && assemblyPtrs.count(cube.get())) entry.actor = compound;
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
        }
    }
}

void Physics::createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace) {
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
    RCBN_LOG("Weld \"" << weld->Name << "\" created (group size: " << assembly.size() << ")");
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
        physx::PxRigidDynamic* oldCompound = weld->m_compound;

        // 1. 旧 compound を共有していた全キューブを収集
        std::vector<std::shared_ptr<BaseCube>> oldGroupCubes;
        if (oldCompound) {
            for (auto& entry : cubes) {
                auto cube = entry.cube.lock();
                if (cube && cube->actor == oldCompound)
                    oldGroupCubes.push_back(cube);
            }
            // 旧 compound を破棄
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
                        if (nb) { processed.insert(nb.get()); bfsQ.push(nb); }
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