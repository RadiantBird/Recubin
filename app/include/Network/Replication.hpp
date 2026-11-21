#pragma once
#include <Network/NetworkTypes.hpp>
#include <Network/SnapshotClock.hpp>
#include <Math/CFrame.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Workspace;
class User;
class Model;
class BaseCube;
class Instance;
class Humanoid;
class Physics;
class Gyro;
class Cube;
class Seat;
class Motor;
struct ReplicationTestAccess;

// ワールドレプリケーション層。NetworkManager(トランスポート)の上に載り、
// Instance/Workspaceを知る側の同期処理をここに隔離する。
// - アバター同期: 各ピアの自キャラRoot姿勢をHost経由で全員に配布し、
//   リモートピアのアバター(PlayerCharacter_<id>)をローカル世界に表示する。
// リモートUserのCharacterは対応するアバターモデルを参照する。
// - 演算主導権: 自キャラと、自分が運転している乗り物(着席したSeatと拘束でつながる部品一式)は
//   Clientが演算して姿勢をHostへ送る。それ以外の同期物体はHostが演算して配る。

// 送信元の物理tickを付けた姿勢。補間表示に使う
struct TimedPose {
    uint32_t tick = 0;
    CFrame pose;
};

class ReplicationManager {
public:
    ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot);
    ~ReplicationManager();

    // 毎フレーム、NetworkManager::update()の後・物理更新の前に呼ぶこと。
    void update(float dt, Physics* physics);
    // NetworkManager::onGameMessage から配線される(packet破棄前の同期呼び出し)。
    void onGameMessage(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId);
    // NetworkManager::onRoleChanged から配線される。
    void onNetworkRoleChanged(NetworkRole oldRole, NetworkRole newRole);
    // active Workspace切替の唯一の入口。WorkspaceとPhysics参照、proxy、
    // world mapping、clock bindingを同じ世代で交換する。
    void setWorkspace(std::shared_ptr<Workspace> workspace);
    std::shared_ptr<Workspace> getWorkspace() const { return m_workspace; }
    Physics* getBoundPhysics() const { return m_physics; }
    std::uint64_t getWorkspaceGeneration() const { return m_workspaceGeneration; }
    bool hasFatalIdentityError() const { return m_fatalIdentityError; }
    // 実機検証専用(--debug-inject-nan-avatar-batch)。Hostが約1秒ごとにNaN座標のAvatarBatchを追加送信する。
    void setDebugInjectNaNAvatarBatch(bool enabled);

private:
    friend struct ReplicationTestAccess;

    struct RemoteAvatar {
        std::shared_ptr<Model> model;
        std::shared_ptr<User> identity; // System.Users配下のUser identity(見つからない場合はnullptrのまま)
        // Rootを先頭に含むリグパーツ。共有native bodyは表示同期時に一度だけ移動する。
        std::vector<std::pair<BaseCube*, CFrame>> parts;
        // spawn時(非衝突化する前)のテンプレートCanCollide。Host物理プロキシ化で復元する
        std::unordered_map<BaseCube*, bool> templateCanCollide;
        CFrame current;         // 表示中の姿勢
        bool   hasPose = false; // 初回受信前はfalse
        // Client: Host tick付きの受信姿勢 / Host: そのClientのtick付きの受信姿勢(補間用)
        std::deque<TimedPose> snapshots;
        bool   gravityDisabled = false;  // 表示専用・追従するボディの重力を切ったか
        std::shared_ptr<Humanoid> humanoid; // Host側物理プロキシとして使う場合はHumanoidを保持(Step2で使用)
        bool   isPhysicsProxy = false;      // true = Hostで受信姿勢へ速度追従する物理ボディ(接触判定用)
        float  walkCycle = 0.0f;            // 受信した歩行アニメ位相
        bool   grounded = true;             // 受信した接地状態
        bool   seated = false;              // Host権威の着席状態
        bool   ragdoll = false;             // Host権威のRagdoll状態
        float  forwardAxis = 0.0f;          // Host: Clientの操作軸(着席中はSeat.Throttle/Steerへ書く)
        float  rightAxis = 0.0f;
        std::weak_ptr<Seat> ownedSeat;      // Host: 主導権を渡した乗り物の着席先
    };

    void sendAvatarUpdates(float dt);   // 20Hz: Client=AvatarState送信 / Host=自姿勢記録+AvatarBatch配布
    void hostSendSimulationClock(float dt, Physics* physics);
    // Host: 各プレイヤーの着席先が変わったら(新規ピア参加時は全員分)SeatStateを配る
    void hostSendSeatStates();
    // Client: SeatStateのうち自分の着席状態だけをローカルへ反映する
    void clientApplySeatState(const uint8_t* payload, size_t len);
    void applyLocalSeatPath(const std::string& seatPath);
    void reconcileAvatars();            // ロスターと生成済みアバターの突き合わせ(生成/破棄)
    void applyAvatarPoses(float dt);    // 受信姿勢を平滑補間して全リグをworld deltaで移動
    bool getLocalRootCFrame(CFrame& out) const; // 自キャラRootのワールド姿勢。無ければfalse
    void spawnRemoteAvatar(PeerId id);
    void despawnRemoteAvatar(PeerId id);
    void despawnAllAvatars();
    // Host: 各物理プロキシを用意し、受信した歩行状態の表示と、着席中の操作軸のSeatへの書き込みを行う
    void hostSimulateAvatars(float dt, Physics* physics);
    // Host: ClientのAvatarStateを受け取り、そのClientの時計と姿勢列に積む
    void hostReceiveAvatarState(PeerId id, const uint8_t* payload, size_t len);
    // Host: 固定stepごとに、各プロキシのRootを受信姿勢の補間へ速度で追従させる
    void hostStepProxies(Physics& physics, float dt);
    void enablePhysicsProxy(RemoteAvatar& avatar, PeerId id, Physics* physics); // Root物理化+Humanoid有効化+NoCollision登録
    bool hasPendingPhysicsRegistration(const RemoteAvatar& avatar) const;

    std::shared_ptr<Workspace> m_workspace;
    std::shared_ptr<User>      m_user;
    Instance*                  m_characterSearchRoot = nullptr; // StarterCharacter探索用(game_mainのsystem)

    std::unordered_map<PeerId, CFrame> m_latestPoses; // 最新姿勢(Host: 自分+全プロキシ / Client: Batch由来)
    std::unordered_map<PeerId, Vector3> m_latestVels; // Host: 各ピアRootの最新線速度(AvatarBatch配布用)
    std::unordered_map<PeerId, SnapshotClock> m_peerClocks; // Host: 各Clientのtickを追う補間時計
    std::unordered_map<PeerId, RemoteAvatar> m_remoteAvatars;
    float m_avatarSendTimer = 0.0f;
    float m_simulationClockTimer = 0.0f;
    size_t m_simulationClockRosterSize = 0;
    bool m_forceReliableSimulationClock = false;
    Physics* m_physics = nullptr;
    std::uint64_t m_workspaceGeneration = 1;
    bool m_warnedPhysicsMismatch = false;

    bool m_pendingProxyUpgradeAll = false; // Client→Host昇格時にセットされ、次のupdate()で全既存アバターをプロキシ化する
    bool m_fatalIdentityError = false;

    // ---- ワールドオブジェクト同期 ----
    // Host: 非Anchoredオブジェクトを列挙してnetIdを採番し、cframeを配布する。
    // Client: マッピング対象をAnchored化(キネマティック)して受信cframeを平滑適用する。
    struct HostSyncedObject {
        std::weak_ptr<BaseCube> cube;
        CFrame lastSent;
        bool   hasSent   = false;
        float  tailTimer = 0.0f; // 停止後もこの秒数だけ送り続ける(ロス対策)
        PeerId owner = 0;        // 0以外 = このClientが演算している(Host側はキネマティックで追従)
        std::deque<TimedPose> snapshots; // 所有Clientのtick付きの受信姿勢(補間用)
        Vector3 lastLinearVelocity{};    // 所有Clientから最後に受けた速度(主導権を戻すときに引き継ぐ)
        Vector3 lastAngularVelocity{};
    };
    struct ClientSyncedObject {
        std::weak_ptr<BaseCube> cube;
        CFrame target;            // 最新の受信姿勢
        bool   hasTarget = false;
        std::deque<TimedPose> snapshots; // Host tick付きの受信姿勢(補間用)
        bool   owned = false;     // 自分が演算している(動的。受信姿勢では動かさない)
    };
    // Host: 主導権を渡した機構内のMotor。駆動値が変わったら所有Clientへ送る
    struct OwnedMotor {
        std::weak_ptr<Motor> motor;
        PeerId owner = 0;
        bool   hasSent = false;
        float  driveVelocity = 0.0f;
        float  maxForce = 0.0f;
        bool   servo = false;
        float  targetAngle = 0.0f;
    };

    void hostUpdateWorld(float dt);     // 再スキャン+マッピング配布+WorldTransforms送信
    void hostRescanWorld();             // 同期対象の列挙とnetId採番。変化があればm_worldMappingDirty
    void hostBroadcastMapping();
    void hostSendWorldTransforms(bool forceAll);
    void clientApplyWorldMapping(const uint8_t* payload, size_t len);
    void clientStoreWorldTransforms(const uint8_t* payload, size_t len);
    void clientApplyWorldSmoothing(float dt);

    // ---- 演算主導権 ----
    // Host: Seatと拘束(Weld/Motor等)でつながる、キャラ以外の同期物体のnetIdとMotorを集める
    void collectMechanism(const std::shared_ptr<Seat>& seat, std::vector<uint32_t>& netIds,
                          std::vector<std::shared_ptr<Motor>>& motors) const;
    // Host: リモートプレイヤーの着席先が変わったら、乗り物の主導権を渡す/取り戻す
    void hostUpdateOwnership();
    void hostAssignOwnership(PeerId id, const std::shared_ptr<Seat>& seat);
    void hostReclaimOwnership(PeerId id);
    void hostSendOwnership();
    void hostSendJointDrives();
    void hostReceiveOwnedTransforms(PeerId id, const uint8_t* payload, size_t len);
    // Host: 固定stepごとに、所有Clientから受けた姿勢の補間をキネマティック目標として与える
    void hostStepOwnedObjects(Physics& physics);
    void clientApplyOwnership(const uint8_t* payload, size_t len);
    void clientApplyJointDrive(const uint8_t* payload, size_t len);
    void clientSendAvatarState();
    void clientSendOwnedTransforms();
    // Client: 自キャラがローカルで降車したらHostへ知らせる
    void clientNotifyStandUp();
    std::unordered_map<uint32_t, PeerId> m_ownership; // Host: netId→所有Client(載っていない=Host)
    std::vector<OwnedMotor> m_ownedMotors;
    bool m_ownershipDirty = false;
    size_t m_ownershipRosterSize = 0;  // Host: 新規ピア検出(増えたら主導権表を再送)
    bool m_localWasSeated = false;     // Client: 前フレームに自キャラが着席していたか
    float m_clientSendTimer = 0.0f;    // Client: 姿勢送信(20Hz)

    // ---- 補間表示 ----
    // Client: Hostの時計を進める
    void clientAdvanceRenderClock(float dt);
    float interpolationDelaySeconds() const;
    double interpolationDelayTicks() const;
    // 固定stepで1回、目標姿勢をnative bodyへ与える(遠い目標へは瞬間移動)
    static void driveKinematicBody(Physics& physics, BaseCube& cube, const CFrame& pose,
                                   std::unordered_set<std::uint64_t>& drivenBodies);
    // tickが新しい姿勢だけを末尾に積む(古い・重複は捨てる)
    static void pushSnapshot(std::deque<TimedPose>& snapshots, uint32_t tick, const CFrame& pose);
    // tick時点の姿勢を前後の姿勢から補間する(範囲外は端の姿勢)。不要になった古い姿勢は捨てる
    static bool sampleSnapshots(std::deque<TimedPose>& snapshots, double tick, CFrame& out);
    uint32_t hostTickForSend() const;

    // 物理ボディを持つ同期物体・プロキシは、固定stepごとにそのstep後の時刻の補間姿勢を
    // 目標として渡す(フレームごとだとstep数の揺れで行き過ぎ・停止が起きる)
    void ensurePreStepCallback();
    void onPreStep(Physics& physics, float dt);
    void clientStepWorldObjects(Physics& physics);
    std::shared_ptr<int> m_lifetimeToken = std::make_shared<int>(0); // Physicsのコールバック登録の生存確認用
    Physics* m_preStepCallbackPhysics = nullptr;
    static constexpr float KINEMATIC_TELEPORT_DISTANCE = 4.0f; // 1stepでこれ以上離れた目標へは瞬間移動する

    SnapshotClock m_hostClock; // Client: Hostのtickを追う補間時計
    void clientReleaseWorldObjects();   // 全対象をAnchored=falseに復元してクリア(昇格/切断時)

    std::unordered_map<std::string, uint32_t>        m_pathToNetId;   // Host: パス→netId(永続採番)
    std::unordered_map<uint32_t, HostSyncedObject>   m_hostObjects;   // Host: netId→対象
    std::unordered_map<uint32_t, ClientSyncedObject> m_clientObjects; // Client: netId→対象
    uint32_t m_nextNetId          = 1;
    bool     m_worldMappingDirty  = false;
    float    m_worldRescanTimer   = 0.0f; // 1秒ごと
    float    m_worldSendTimer     = 0.0f; // 20Hz
    float    m_worldSnapshotTimer = 0.0f; // 5秒ごと全量送信
    bool     m_worldSnapshotPending = false; // 次の送信サイクルで全量送信する(タイマーずれで失われないよう保持)
    size_t   m_prevRosterSize     = 0;    // 新規ピア検出(増えたらマッピング再配布)

    // キャラの全リグボディを同じ並進量で動かし、線速度にvelocityDeltaを足す(回転は変えない)
    void translateCharacter(Instance& character, const Vector3& offset, const Vector3& velocityDelta);

    void sendDebugNaNAvatarBatch(const std::vector<std::pair<PeerId, CFrame>>& entries); // Host: 検証用NaN注入

    bool m_debugInjectNaNAvatarBatch = false; // Host: 検証用にNaN座標のAvatarBatchを追加送信する
    uint32_t m_debugInjectBatchCounter = 0;   // Host: 通常AvatarBatchの送信回数(注入間隔の計数用)
    uint32_t m_debugInjectSentCount = 0;      // Host: 注入したNaN AvatarBatchの数(ログ用)
    std::unordered_map<PeerId, std::string> m_sentSeatPaths; // Host: 最後に配った各プレイヤーの着席先
    size_t m_seatStateRosterSize = 0;                         // Host: 新規ピア検出(増えたら全員分を再送)
};
