#pragma once
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

    struct NoCollisionEntry {
        std::weak_ptr<Instance> inst;   // NoCollision インスタンス
        std::weak_ptr<BaseCube> c0, c1;
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
    static std::function<void(BaseCube*, BaseCube*)> s_contactCallback;

    void init();
    virtual ~Physics();
    void update(Workspace& workspace, float dt);
    // このPhysicsインスタンス自身のシーンを、Workspace非依存に1回分だけ進める
    // (蓄積dtでの固定ステップsimulate/fetchResults + 浮力/Force適用 + 遅延op処理)。
    // Workspace構造変化(pendingInstances/pendingConstraints)の取り込みは行わない。
    // 予測専用シーン(Workspaceを持たない)からも呼べるようにupdate()から切り出した。
    void stepOnce(float dt);
    // 現在登録されている全cubeについて、Actorの姿勢をcframeへ同期する(syncPhysics呼び出し)。
    void syncAllCubes();
    // アンカー駆動のキネマティックWeld(帽子等)を、アニメ更新後に即時同期する。
    // フレームループ内で processInput(Humanoidのパーツ配置)の後・描画の前に呼ぶことで、
    // 帽子がHead等のアニメ駆動部にラグ無く追従する。
    void syncWeldKinematics();
    // memberを指定ワールド姿勢へ移動する。Weld連結体なら全メンバーを剛体として追従させ、
    // actor未作成時もCFrameへ同じ変換を反映するため、Toolの装備直後にも使用できる。
    void moveWeldAssembly(const std::shared_ptr<BaseCube>& member, const CFrame& worldCFrame);
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
    void createBallSocket(const std::shared_ptr<BallSocket>& bs);
    void createNoCollision(const std::shared_ptr<NoCollision>& nc);
    void removeConstraint(const std::shared_ptr<Instance>& c);

    bool raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hitResult, physx::PxRigidActor* ignoreActor = nullptr);

    // cube と AABB が重なっている、className の BaseCube 系インスタンスを1つ探す(cube自身は除外)。
    // 水没判定(LiquidCube)・Truss接触・Seat接触で共用する近似的な「接触」判定
    BaseCube* findOverlapping(const BaseCube& cube, const std::string& className, float margin = 0.0f) const;

    void setGravity(const Vector3& g);
    Vector3 getGravity() const;

    // ---- Terrain 用アクセサ ----
    // buildChunkPhysics() から呼ばれる。
    physx::PxScene*   getScene()   const { return scene; }
    static physx::PxPhysics* GetPhysics() { return s_pxPhysics; }
};
