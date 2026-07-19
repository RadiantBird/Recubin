#pragma once
#include <Network/NetworkTypes.hpp>
#include <Math/CFrame.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Workspace;
class User;
class Model;
class BaseCube;
class Instance;

// ワールドレプリケーション層。NetworkManager(トランスポート)の上に載り、
// Instance/Workspaceを知る側の同期処理をここに隔離する。
// - アバター同期: 各ピアの自キャラRoot姿勢をHost経由で全員に配布し、
//   リモートピアのアバター(RemotePlayer_<id>)をローカル世界に表示する。
// リモートアバターは CanCollide=false の純視覚(アクター無し)。User.Characterには決して代入しない。
class ReplicationManager {
public:
    ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot);

    // 毎フレーム、NetworkManager::update()の後・物理更新の前に呼ぶこと。
    void update(float dt);
    // NetworkManager::onGameMessage から配線される(packet破棄前の同期呼び出し)。
    void onGameMessage(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId);
    // NetworkManager::onRoleChanged から配線される。
    void onNetworkRoleChanged(NetworkRole oldRole, NetworkRole newRole);

private:
    struct RemoteAvatar {
        std::shared_ptr<Model> model;
        // Root含む全パーツと、Root相対のローカルオフセット(剛体一括追従用)。生ポインタの所有はmodel。
        std::vector<std::pair<BaseCube*, CFrame>> parts;
        CFrame current;         // 平滑表示中の姿勢
        bool   hasPose = false; // 初回受信前はfalse(初回はスナップ)
    };

    void sendAvatarUpdates(float dt);   // 20Hz: Client=AvatarState送信 / Host=自姿勢記録+AvatarBatch配布
    void reconcileAvatars();            // ロスターと生成済みアバターの突き合わせ(生成/破棄)
    void applyAvatarPoses(float dt);    // 受信姿勢を平滑補間してパーツcframeへ書き込み
    bool getLocalRootCFrame(CFrame& out) const; // 自キャラRootのワールド姿勢。無ければfalse
    void spawnRemoteAvatar(PeerId id);
    void despawnRemoteAvatar(PeerId id);
    void despawnAllAvatars();

    std::shared_ptr<Workspace> m_workspace;
    std::shared_ptr<User>      m_user;
    Instance*                  m_characterSearchRoot = nullptr; // StarterCharacter探索用(game_mainのsystem)

    std::unordered_map<PeerId, CFrame> m_latestPoses; // 最新受信姿勢(Host: 自分+全Client / Client: Batch由来)
    std::unordered_map<PeerId, RemoteAvatar> m_remoteAvatars;
    float m_avatarSendTimer = 0.0f;

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
};
