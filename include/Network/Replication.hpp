#pragma once
#include <Network/NetworkTypes.hpp>
#include <Math/CFrame.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
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
struct ReplicationTestAccess;

// ワールドレプリケーション層。NetworkManager(トランスポート)の上に載り、
// Instance/Workspaceを知る側の同期処理をここに隔離する。
// - アバター同期: 各ピアの自キャラRoot姿勢をHost経由で全員に配布し、
//   リモートピアのアバター(PlayerCharacter_<id>)をローカル世界に表示する。
// リモートUserのCharacterは対応するアバターモデルを参照する。

// Client→Host間で送受信する、固定step 1回分の入力。ClientのHumanoidがそのstepで
// 使った入力そのもので、HostのリモートキャラはHost側の1stepで同じ入力を1回だけ使う
struct AvatarInputWire {
    // Vector3はコンストラクタを持たない集合体のため、明示的に{}が無いと未受信時にゴミ値になる
    Vector3 flatForward{}, flatRight{}, targetMoveDir{};
    bool hasInput = false;        // このstepに移動入力があったか(無ければ移動させない)
    bool isPressingMove = false;
    bool ctrlLockEnabled = false;
    bool jump = false;            // このstepで実行する単発アクション
    bool standUp = false;
    float forwardAxis = 0.0f, rightAxis = 0.0f;
    float smoothing = 0.15f;      // ClientのUser.CharacterSmoothing
    uint32_t seq = 0;             // Clientの固定step通し番号(1step=1入力、連番)
};

// Client: 固定stepごとに溜める入力履歴。Hostからackが来たら、同じseqの適用前状態と
// Host権威状態を比較し、ズレがあるときだけ補正する。
struct BufferedInput {
    uint32_t seq = 0;
    AvatarInputWire input;
    Vector3 rootPositionBefore{}; // このstepの入力を適用する"前"の自キャラRoot位置
    Vector3 rootVelocityBefore{}; // 同、Root線速度
};

// Hostの物理tickを付けた姿勢。Clientの補間表示に使う
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
        std::deque<TimedPose> snapshots; // Client: Host tick付きの受信姿勢(補間用)
        bool   gravityDisabled = false;  // Client: 表示専用ボディの重力を切ったか
        std::shared_ptr<Humanoid> humanoid; // Host側物理プロキシとして使う場合はHumanoidを保持(Step2で使用)
        bool   isPhysicsProxy = false;      // true = Host権威で物理シミュレート中(Step2で設定)
        bool   inputCallbackInstalled = false; // Host: 固定stepごとの入力供給をHumanoidへ登録済みか
        float  walkCycle = 0.0f;            // Hostから受信した歩行アニメ位相
        bool   grounded = true;             // Host権威の接地状態
        bool   seated = false;              // Host権威の着席状態
        bool   ragdoll = false;             // Host権威のRagdoll状態
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
    void reconcileLocalPose(); // Client: Hostから受信した自分の権威姿勢とローカル予測のズレが大きければスナップ補正する
    bool getLocalRootCFrame(CFrame& out) const; // 自キャラRootのワールド姿勢。無ければfalse
    void spawnRemoteAvatar(PeerId id);
    void despawnRemoteAvatar(PeerId id);
    void despawnAllAvatars();
    void hostSimulateAvatars(float dt, Physics* physics); // Host: 各物理プロキシを用意し、固定stepごとの入力供給を登録する
    // Host: 受信した入力をピアの入力バッファへ入れる(処理済み・重複は捨てる)
    void hostQueueInputs(PeerId id, const std::vector<AvatarInputWire>& inputs);
    // Host: リモートキャラの固定step 1回分。バッファから次のseqを1つ取り出して適用し、
    // 適用前の状態をそのseqのack用に記録する
    void hostConsumeInputStep(PeerId id, Humanoid& humanoid);
    void enablePhysicsProxy(RemoteAvatar& avatar, PeerId id, Physics* physics); // Root物理化+Humanoid有効化+NoCollision登録
    bool hasPendingPhysicsRegistration(const RemoteAvatar& avatar) const;

    std::shared_ptr<Workspace> m_workspace;
    std::shared_ptr<User>      m_user;
    Instance*                  m_characterSearchRoot = nullptr; // StarterCharacter探索用(game_mainのsystem)

    std::unordered_map<PeerId, CFrame> m_latestPoses; // 最新受信姿勢(Host: 自分+全Client / Client: Batch由来)
    std::unordered_map<PeerId, Vector3> m_latestVels; // Host: 各ピアRootの最新線速度(AvatarBatch配布用)
    std::unordered_map<PeerId, RemoteAvatar> m_remoteAvatars;
    float m_avatarSendTimer = 0.0f;
    float m_simulationClockTimer = 0.0f;
    size_t m_simulationClockRosterSize = 0;
    bool m_forceReliableSimulationClock = false;
    Physics* m_physics = nullptr;
    std::uint64_t m_workspaceGeneration = 1;
    bool m_warnedPhysicsMismatch = false;

    // Host: ピアごとの入力バッファ。開始後はcursor(次に使うseq)以上だけを保持する
    struct PeerInputQueue {
        std::map<uint32_t, AvatarInputWire> inputs;
        uint32_t cursor = 0;
        bool started = false;
        AvatarInputWire last;   // 入力が届かないstepで移動だけ続けるための直前入力
        bool hasLast = false;
    };
    static constexpr size_t INPUT_BUFFER_START = 3;  // この数だけ溜まってから消費を始める(到着の揺れの吸収)
    static constexpr size_t INPUT_BUFFER_MAX = 12;   // これを超えたら古い入力を捨てて追いつく
    static constexpr size_t MAX_INPUTS_PER_PACKET = 32;
    std::unordered_map<PeerId, PeerInputQueue> m_inputQueues;
    bool m_pendingProxyUpgradeAll = false; // Client→Host昇格時にセットされ、次のupdate()で全既存アバターをプロキシ化する
    bool m_fatalIdentityError = false;

    CFrame m_hostAuthoritativeSelfPose;
    bool   m_hasHostAuthoritativeSelfPose = false;
    Vector3 m_hostAuthoritativeSelfVel{}; // Client: 権威姿勢と同時に受信した自Rootの線速度

    // ---- ワールドオブジェクト同期 ----
    // Host: 非Anchoredオブジェクトを列挙してnetIdを採番し、cframeを配布する。
    // Client: マッピング対象をAnchored化(キネマティック)して受信cframeを平滑適用する。
    struct HostSyncedObject {
        std::weak_ptr<BaseCube> cube;
        CFrame lastSent;
        bool   hasSent   = false;
        float  tailTimer = 0.0f; // 停止後もこの秒数だけ送り続ける(ロス対策)
    };
    struct ClientSyncedObject {
        std::weak_ptr<BaseCube> cube;
        CFrame target;            // 最新の受信姿勢
        bool   hasTarget = false;
        std::deque<TimedPose> snapshots; // Host tick付きの受信姿勢(補間用)
    };

    void hostUpdateWorld(float dt);     // 再スキャン+マッピング配布+WorldTransforms送信
    void hostRescanWorld();             // 同期対象の列挙とnetId採番。変化があればm_worldMappingDirty
    void hostBroadcastMapping();
    void hostSendWorldTransforms(bool forceAll);
    void clientApplyWorldMapping(const uint8_t* payload, size_t len);
    void clientStoreWorldTransforms(const uint8_t* payload, size_t len);
    void clientApplyWorldSmoothing(float dt);

    // ---- Client: Host tick基準の補間表示 ----
    // 受信したHost tickを記録する(新しいものだけ)
    void clientNoteHostTick(uint32_t tick);
    // 表示時刻(Host tick単位)を進め、「推定Host現在tick - 補間遅延」へ緩やかに合わせる
    void clientAdvanceRenderClock(float dt);
    float interpolationDelaySeconds() const;
    // tickが新しい姿勢だけを末尾に積む(古い・重複は捨てる)
    static void pushSnapshot(std::deque<TimedPose>& snapshots, uint32_t tick, const CFrame& pose);
    // tick時点の姿勢を前後の姿勢から補間する(範囲外は端の姿勢)。不要になった古い姿勢は捨てる
    static bool sampleSnapshots(std::deque<TimedPose>& snapshots, double tick, CFrame& out);
    uint32_t hostTickForSend() const;

    uint32_t m_latestHostTick = 0;
    bool     m_hasHostTick = false;
    double   m_ticksSinceHostTick = 0.0; // 最新Host tick受信からの経過(tick単位)
    double   m_renderTick = 0.0;
    bool     m_hasRenderTick = false;
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

    // Client: 自キャラの全リグボディを同じ並進量で動かし、線速度にvelocityDeltaを足す(回転は変えない)
    void translateLocalCharacter(const Vector3& offset, const Vector3& velocityDelta);
    // Client: 自キャラのHumanoidへ固定stepごとの入力記録を登録する(キャラ交代時は付け替える)
    void clientEnsureInputRecording();
    void clientDetachInputRecording();
    // Client: 固定step 1回分の入力と適用前の状態を履歴へ記録する
    void clientRecordInputStep(Humanoid& humanoid);
    // Client: 未ackの入力をまとめて送る(毎フレーム。ロスしても次で届く)
    void clientSendInputs();

    void sendDebugNaNAvatarBatch(const std::vector<std::pair<PeerId, CFrame>>& entries); // Host: 検証用NaN注入

    std::deque<BufferedInput> m_inputHistory;
    uint32_t m_nextSeq = 1;
    std::weak_ptr<Humanoid> m_recordingHumanoid; // Client: 入力記録を登録したHumanoid
    bool m_debugInjectNaNAvatarBatch = false; // Host: 検証用にNaN座標のAvatarBatchを追加送信する
    uint32_t m_debugInjectBatchCounter = 0;   // Host: 通常AvatarBatchの送信回数(注入間隔の計数用)
    uint32_t m_debugInjectSentCount = 0;      // Host: 注入したNaN AvatarBatchの数(ログ用)
    std::unordered_map<PeerId, std::string> m_sentSeatPaths; // Host: 最後に配った各プレイヤーの着席先
    size_t m_seatStateRosterSize = 0;                         // Host: 新規ピア検出(増えたら全員分を再送)

    std::unordered_map<PeerId, uint32_t> m_lastProcessedSeq; // Host: 各ピアの最後に処理したseq(0=未受信)

    uint32_t m_hostAckedSeq = 0; // Client: Hostが最後に処理したと報告した自分のseq(m_hasHostAuthoritativeSelfPose頒受信時に同時取得)
};
