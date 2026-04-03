#include "include/Core/Physics.hpp"

#define RECUBIN_DEBUG
#ifdef RECUBIN_DEBUG
    #define d_print(x) std::cout << "[DEBUG] " << x << std::endl
#else
    // 何も定義しない = 呼び出し箇所は「無」になる
    #define d_print(x) ((void)0) 
#endif

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
        physx::PxVec3(cube->Position.x, cube->Position.y, cube->Position.z),
        physx::PxQuat(cube->Rotation.x, cube->Rotation.y, cube->Rotation.z, cube->Rotation.w)
    );

    physx::PxRigidActor* actor = nullptr;
    if (cube->Anchored) {
        actor = physics->createRigidStatic(transform);
    } else {
        actor = physics->createRigidDynamic(transform);
    }

    // 形状とマテリアルの紐付け
    physx::PxRigidActorExt::createExclusiveShape(*actor, geometry, *defaultMaterial);
    
    scene->addActor(*actor);
    cube->actor = actor; // BaseCube側に参照を戻す
}

void Physics::update(Workspace& workspace, float dt) {
    // d_print("Update Frame Start | dt: " << dt);
    static float accumulator = 0.0f;
    const float fixedStep = 1.0f / 60.0f;
    const int MAX_STEPS = 5; // ★安全装置：1フレーム最大5回まで

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
            d_print("WARNING: Physics safety break engaged! (Spiral of Death prevented)");
            break;
        }
    }
    // 1. 未反映の新入りを登録
    for (Instance* inst : workspace.pendingInstances) {
        if (inst->IsA("BaseCube")) {
            createActor(static_cast<BaseCube*>(inst));
        }
    }
    workspace.pendingInstances.clear();

    // 3. 全ての子を同期（ここも本来はリスト化すると速い）
    for (auto const& [name, child] : workspace.getChildren()) {
        if (child->IsA("BaseCube")) {
            static_cast<BaseCube*>(child)->syncPhysics();
        }
    }
}