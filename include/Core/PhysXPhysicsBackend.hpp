#pragma once
#include <include/Core/IPhysicsBackend.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>
#include <functional>
#include <include/Instances/Rope.hpp>
#include <include/Instances/Rod.hpp>
#include <include/Instances/Weld.hpp>
#include <include/Instances/Motor.hpp>
#include <include/Instances/BallSocket.hpp>
#include <include/Instances/NoCollision.hpp>
#include <include/Util/Material.hpp>
#include <include/Math/Quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <set>

class RCBNFilterCallback;

class Physics;

class PhysXPhysicsBackend final : public IPhysicsBackend {
private:
    Physics* m_facade = nullptr;
    static physx::PxFoundation*            s_foundation;
    static physx::PxPhysics*               s_pxPhysics;
    static physx::PxDefaultCpuDispatcher*  s_dispatcher;
    static physx::PxDefaultAllocator       s_allocator;
    static physx::PxDefaultErrorCallback   s_errorCallback;
    static int                             s_refCount;

    physx::PxScene* scene = nullptr;
    // MaterialType ではなく実際の係数でキャッシュ（キューブごと固有値に対応）。
    // 量子化済み int で同値判定し、浮動小数誤差での重複生成を防ぐ。
    struct MatKey {
        int sf, df, rs;
        bool operator==(const MatKey& o) const { return sf==o.sf && df==o.df && rs==o.rs; }
    };
    struct MatKeyHash {
        size_t operator()(const MatKey& k) const {
            size_t h = (size_t)k.sf;
            h = h*131 + (size_t)k.df;
            h = h*131 + (size_t)k.rs;
            return h;
        }
    };
    std::unordered_map<MatKey, physx::PxMaterial*, MatKeyHash> materialCache;
    float m_accumulator = 0.0f;

    struct CubeEntry {
        std::weak_ptr<BaseCube> cube;
        BaseCube* cubeRaw = nullptr;
        physx::PxRigidActor* actor = nullptr;
    };
    std::vector<CubeEntry> cubes;
    // native actor flag は MaintainVelocity でも一時変更されるため、明示的な
    // setGravityEnabled() の状態を Cube 単位で別に保持する。
    std::unordered_map<const BaseCube*, bool> m_gravityEnabled;
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
        PhysicsConstraintHandle handle;
        physx::PxJoint* joint = nullptr;
    };
    std::vector<ConstraintEntry> m_constraints;
    std::uint64_t m_nextConstraintHandle = 1;
    std::set<physx::PxRigidStatic*> m_terrainActors;

    struct NoCollisionEntry {
        std::weak_ptr<Instance> inst;   // NoCollision インスタンス
        std::weak_ptr<BaseCube> c0, c1;
        PhysicsConstraintHandle handle;
    };
    std::vector<NoCollisionEntry> m_noCollisionEntries;
    // フィルターコールバック照合用の正規化済みペア集合（simulate 中は不変のためロック不要）
    std::set<std::pair<const void*, const void*>> m_noCollisionPairs;

    // m_noCollisionEntries から m_noCollisionPairs を作り直す（両cubeがlockできるエントリのみ）
    void rebuildNoCollisionPairSet();
    // cube がいずれかの生きた NoCollision エントリに含まれるか
    bool isInNoCollisionPair(const BaseCube* cube) const;
    // cube の全シェイプの filterData word0 候補ビットを isInNoCollisionPair の結果に応じて更新する
    void applyNoCollisionFilterBit(const std::shared_ptr<BaseCube>& cube);
    PhysicsConstraintHandle allocateConstraintHandle();
    ConstraintEntry* findConstraintEntry(PhysicsConstraintHandle handle);
    void clearConstraintHandle(Instance& constraint);

    void rebuildGroup(const std::vector<std::shared_ptr<BaseCube>>& assembly);
    void removeInvalidConstraints(Workspace& workspace);

    // LiquidCube に侵入した動的キューブへ浮力を加える（simulate 前に呼ぶ）
    void applyBuoyancy();

    // Force インスタンス（BaseCube の子）の力/トルク/速度維持を適用する（simulate 前に呼ぶ）
    void applyForces();

    // 2つのAABB(回転無視)の重なり体積を返す(重ならなければ0)。findOverlappingで使用
    static float aabbOverlapVolume(const Vector3& posA, const Vector3& sizeA, const Vector3& posB, const Vector3& sizeB);

    physx::PxSimulationEventCallback* m_contactCallback = nullptr;
    RCBNFilterCallback* m_filterCallback = nullptr;

public:
    explicit PhysXPhysicsBackend(Physics* facade);
    ~PhysXPhysicsBackend() override;

    bool init() override;
    bool isAvailable() const override;
    PhysicsBackendType getType() const override;
    void update(Workspace& workspace, float dt) override;
    // このPhysicsインスタンス自身のシーンを、Workspace非依存に1回分だけ進める
    // (蓄積dtでの固定ステップsimulate/fetchResults + 浮力/Force適用 + 遅延op処理)。
    // Workspace構造変化(pendingInstances/pendingConstraints)の取り込みは行わない。
    // 予測専用シーン(Workspaceを持たない)からも呼べるようにupdate()から切り出した。
    void stepOnce(float dt) override;
    // 現在登録されている全cubeについて、Actorの姿勢をcframeへ同期する(syncPhysics呼び出し)。
    void syncAllCubes() override;
    // アンカー駆動のキネマティックWeld(帽子等)を、アニメ更新後に即時同期する。
    // フレームループ内で processInput(Humanoidのパーツ配置)の後・描画の前に呼ぶことで、
    // 帽子がHead等のアニメ駆動部にラグ無く追従する。
    void syncWeldKinematics() override;
    // memberを指定ワールド姿勢へ移動する。Weld連結体なら全メンバーを剛体として追従させ、
    // actor未作成時もCFrameへ同じ変換を反映するため、Toolの装備直後にも使用できる。
    void moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame) override;
    void createActor(const std::shared_ptr<BaseCube>& cube) override;
    void recreateActor(const std::shared_ptr<BaseCube>& cube) override;
    void removeCube(const std::shared_ptr<BaseCube>& cube) override;
    void onCubeDestroyed(BaseCube& cube) override;

    void clearCubes() override;

    bool hasBody(const BaseCube& cube) const override;
    bool sharesBody(const BaseCube& first, const BaseCube& second) const override;
    // compoundメンバー個別の姿勢ではなく、共有するnative body原点のワールド姿勢を扱う。
    CFrame getBodyWorldCFrame(const BaseCube& cube) const override;
    void setBodyWorldCFrame(BaseCube& cube, const CFrame& worldCFrame) override;
    Vector3 getLinearVelocity(const BaseCube& cube) const override;
    void setLinearVelocity(BaseCube& cube, const Vector3& velocity) override;
    void setAngularVelocity(BaseCube& cube, const Vector3& velocity) override;
    void setGravityEnabled(BaseCube& cube, bool enabled) override;
    void applyLockFlags(BaseCube& cube) override;
    void syncCube(BaseCube& cube) override;

    void enqueueResize(const std::shared_ptr<BaseCube>& cube) override;
    void enqueueSetRotation(const std::shared_ptr<BaseCube>& cube, Quaternion rot) override;

    void createRope(const std::shared_ptr<Rope>& rope) override;
    void createRod(const std::shared_ptr<Rod>& rod) override;
    void createWeld(const std::shared_ptr<Weld>& weld, Workspace& workspace) override;
    void createMotor(const std::shared_ptr<Motor>& motor) override;
    void createBallSocket(const std::shared_ptr<BallSocket>& bs) override;
    void createNoCollision(const std::shared_ptr<NoCollision>& nc) override;
    void removeConstraint(const std::shared_ptr<Instance>& c) override;
    void updateConstraint(const std::shared_ptr<Instance>& constraint) override;

    bool raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, const BaseCube* ignoreCube = nullptr) override;

    // cube と AABB が重なっている、className の BaseCube 系インスタンスを1つ探す(cube自身は除外)。
    // 水没判定(LiquidCube)・Truss接触・Seat接触で共用する近似的な「接触」判定
    BaseCube* findOverlapping(const BaseCube& cube, const std::string& className, float margin = 0.0f) const override;

    void setGravity(const Vector3& g) override;
    Vector3 getGravity() const override;

    PhysicsTerrainHandle createTerrain(const PhysicsTerrainDescriptor& descriptor) override;
    PhysicsTerrainHandle replaceTerrain(
        PhysicsTerrainHandle oldHandle,
        const PhysicsTerrainDescriptor& descriptor) override;
    void destroyTerrain(PhysicsTerrainHandle handle) override;
};
