#pragma once
#include <Network/NetworkTypes.hpp>
#include <Math/CFrame.hpp>

#include <cstdint>
#include <deque>
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
class Cube;

// ワールドレプリケーション層。NetworkManager(トランスポート)の上に載り、
// Instance/Workspaceを知る側の同期処理をここに隔離する。
// - アバター同期: 各ピアの自キャラRoot姿勢をHost経由で全員に配布し、
//   リモートピアのアバター(PlayerCharacter_<id>)をローカル世界に表示する。
// リモートUserのCharacterは対応するアバターモデルを参照する。

// Client→Host間で送受信する入力スナップショット
struct AvatarInputWire {
    // Vector3はコンストラクタを持たない集合体のため、明示的に{}が無いと未受信時にゴミ値になる
    Vector3 flatForward{}, flatRight{}, targetMoveDir{};
    bool isPressingMove = false;
    bool ctrlLockEnabled = false;
    bool jumpRequested = false;
    float forwardAxis = 0.0f, rightAxis = 0.0f;
    uint32_t seq = 0; // このスナップショットの送信元フレーム通し番号(Client→Host)
};

// Client: フレーム単位で溜める入力履歴。Hostからackが来たら、ack以前は破棄し、
// ack以降をmove()で1件ずつ再生してズレを解消する(Step4で使用)。
struct BufferedInput {
    uint32_t seq = 0;
    AvatarInputWire input;
    float dt = 0.0f;
    // このエントリの入力を適用する"前"の、ローカルHumanoidの内部状態のスナップショット
    // (リプレイ開始時に一番古い未ackエントリのこれへ復元してから再生する)
    Vector3 currentMoveDirBefore{};
    float walkCycleBefore = 0.0f;
    Quaternion rotationBefore{};
};

class ReplicationManager {
public:
    ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot);

    // 毎フレーム、NetworkManager::update()の後・物理更新の前に呼ぶこと。
    void update(float dt, Physics* physics);
    // NetworkManager::onGameMessage から配線される(packet破棄前の同期呼び出し)。
    void onGameMessage(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId);
    // NetworkManager::onRoleChanged から配線される。
    void onNetworkRoleChanged(NetworkRole oldRole, NetworkRole newRole);
    bool hasFatalIdentityError() const { return m_fatalIdentityError; }

private:
    struct RemoteAvatar {
        std::shared_ptr<Model> model;
        std::shared_ptr<User> identity; // System.Users配下のUser identity(見つからない場合はnullptrのまま)
        // Root含む全パーツと、Root相対のローカルオフセット(剛体一括追従用)。生ポインタの所有はmodel。
        std::vector<std::pair<BaseCube*, CFrame>> parts;
        CFrame current;         // 平滑表示中の姿勢
        bool   hasPose = false; // 初回受信前はfalse(初回はスナップ)
        std::shared_ptr<Humanoid> humanoid; // Host側物理プロキシとして使う場合はHumanoidを保持(Step2で使用)
        bool   isPhysicsProxy = false;      // true = Host権威で物理シミュレート中(Step2で設定)
        float  walkCycle = 0.0f;            // Hostから受信した歩行アニメ位相
        bool   grounded = true;             // Host権威の接地状態
        bool   seated = false;              // Host権威の着席状態
    };

    void sendAvatarUpdates(float dt);   // 20Hz: Client=AvatarState送信 / Host=自姿勢記録+AvatarBatch配布
    void hostSendSimulationClock(float dt, Physics* physics);
    void reconcileAvatars();            // ロスターと生成済みアバターの突き合わせ(生成/破棄)
    void applyAvatarPoses(float dt);    // 受信姿勢を平滑補間してパーツcframeへ書き込み
    void reconcileLocalPose(); // Client: Hostから受信した自分の権威姿勢とローカル予測のズレが大きければスナップ補正する
    bool getLocalRootCFrame(CFrame& out) const; // 自キャラRootのワールド姿勢。無ければfalse
    void spawnRemoteAvatar(PeerId id);
    void despawnRemoteAvatar(PeerId id);
    void despawnAllAvatars();
    void hostSimulateAvatars(float dt, Physics* physics); // Host: 各物理プロキシに受信入力を適用して代理move()する
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

    std::unordered_map<PeerId, AvatarInputWire> m_pendingAvatarInput; // Host: 各ピアの最新受信入力
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
        CFrame target;
        CFrame current;
        bool   hasTarget = false;
        bool   snapped   = false; // 初回target受信時にcurrentへスナップ済みか
    };

    void hostUpdateWorld(float dt);     // 再スキャン+マッピング配布+WorldTransforms送信
    void hostRescanWorld();             // 同期対象の列挙とnetId採番。変化があればm_worldMappingDirty
    void hostBroadcastMapping();
    void hostSendWorldTransforms(bool forceAll);
    void clientApplyWorldMapping(const uint8_t* payload, size_t len);
    void clientStoreWorldTransforms(const uint8_t* payload, size_t len);
    void clientApplyWorldSmoothing(float dt);
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

    // ---- クライアント予測用の分離シーン(Step2以降で使用) ----
    // メインのWorkspace物理シーンは全オブジェクトが同居するため巻き戻せない。
    // ローカルプレイヤー専用の複製Root("シャドウ")だけを予測シーンに置き、
    // 静的な地形(Anchored && CanCollide)だけをミラーリングして、
    // 入力リプレイによる補正計算に使う(Seat/Truss/LiquidCube/Terrainはミラー対象外)。
    void ensurePredictionScene();               // 遅延生成(初回のClient役割フレームで呼ぶ)
    void syncPredictionShadowToLocal();          // 毎フレーム: シャドウRootを本物のRootへ強制同期
    void rescanPredictionStaticGeometry();       // 約2秒ごと: 静的ジオメトリの差分ミラーリング

    std::unique_ptr<Physics> m_predictionPhysics;
    std::shared_ptr<Cube> m_shadowRoot;
    std::shared_ptr<Humanoid> m_predictionHumanoid;
    std::unordered_map<std::string, std::shared_ptr<BaseCube>> m_predictionStaticMirror; // path -> ミラーCube
    float m_predictionRescanTimer = 0.0f;
    bool m_predictionSceneReady = false;

    void bufferLocalInput(float dt); // Client: 毎フレーム、現在のlastMovementInputを履歴に積む

    std::deque<BufferedInput> m_inputHistory;
    uint32_t m_nextSeq = 1;
    bool m_pendingJumpLatch = false; // Client: 前回AvatarState送信以降にジャンプ要求があったか(20Hz間引きでタップを取りこぼさないためのラッチ)

    std::unordered_map<PeerId, uint32_t> m_lastProcessedSeq; // Host: 各ピアの最後に処理したseq(0=未受信)

    uint32_t m_hostAckedSeq = 0; // Client: Hostが最後に処理したと報告した自分のseq(m_hasHostAuthoritativeSelfPose頒受信時に同時取得)
};
