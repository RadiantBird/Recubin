#pragma once
#include <Network/NetworkTypes.hpp>
#include <Math/Vector3.hpp>

#include <enet/enet.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// v2.0 ネットワーク基盤(基盤tier)の中核クラス。
// Host起動/Client接続/ENetポーリング/RELIABLE-UNRELIABLEチャンネル送受信のみを担う。
// リソーススコア計測によるホスト自動選出・動的ロール切替の実処理はこのセッションのスコープ外
// (下部のTODOコメント参照)。役割(Role)自体はgetRole()で毎フレーム参照される設計のため、
// 将来Roleが切り替わればLuauEngine側のスクリプト実行フィルタは自動的に追従する。
class NetworkManager {
public:
    static NetworkManager& get();

    // port で Listen し、Role を Host にする。enet_initialize が未実行なら内部で行う。
    bool startHost(uint16_t port);
    // address:port の Host へ接続する。Role を Client にする。
    // listenPort = 自分がHost昇格した場合にListenするポート。Helloで申告する。
    bool connect(const std::string& address, uint16_t port, uint16_t listenPort);
    // 接続を閉じてRoleをOfflineへ戻す。
    void shutdown();

    // 毎フレーム呼ぶこと。poll＋周期送信(ResourceReport/Roster配布)を行う。
    void update(float dt);

    NetworkRole getRole() const { return m_role; }
    bool isActive() const { return m_host != nullptr; }
    // モックデモ用: 1件でも接続済みPeerがあるか(接続確立の簡易検出に使う)
    bool hasPeers() const { return !m_peers.empty(); }

    PeerId getLocalPeerId() const { return m_localPeerId; }
    const std::vector<PeerInfo>& getRoster() const { return m_roster; }
    MigrationState getMigrationState() const { return m_migrationState; }

    // ---- 送信API ----
    // RELIABLEチャンネルで全Peer(Hostの場合)/Host(Clientの場合)へブロードキャストする。
    void sendChatMessage(const std::string& text);

    // 指定チャンネルで接続中の全Peerへ生ペイロードをブロードキャストする。
    // 先頭1バイトは MessageType であること(ReplicationManager が組み立てる)。
    // Client視点のPeerはHostのみなので、両ロールとも「相手全員に送る」で成立する。
    bool sendBytes(const std::vector<uint8_t>& payload, NetworkChannel channel);

    // ロール確定時(Offline→Host/Client、Client→Host昇格、shutdown)に呼ばれる。
    // game_main が Luau への通知(System.NetworkRoleChanged)を配線する。
    std::function<void(NetworkRole oldRole, NetworkRole newRole)> onRoleChanged;

    // AvatarState〜WorldTransforms 受信時に呼ばれる(packet破棄前の同期呼び出し)。
    // senderId: Host視点=送信元ClientのPeerId / Client視点=1(Host)。
    std::function<void(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId)> onGameMessage;

    static const char* roleToString(NetworkRole role); // "Offline" / "Host" / "Client"

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // 現状: 応用tierの3項目(リソース計測・ホスト自動選出・動的ロール切替)は実装済み。
    // 残タスク: NetworkEvent(Luau向けRemoteEvent相当)、NAT越え。ワールドレプリケーションはReplication.hppに実装。

private:
    NetworkManager() = default;
    ~NetworkManager();

    void poll();
    void handleEvent(const ENetEvent& event);
    void broadcastPacket(ENetPacket* packet, NetworkChannel channel);
    void changeRole(NetworkRole newRole);

    void sendHello(); // Client→Host。listenPortと現在のm_localPeerIdを送る
    void broadcastRoster(); // Host専用
    void logRoster() const;
    static float measureCpuScore();

    void updateMigration(float dt);
    // ロスターから旧Host(isHost==true)を除いた候補のうち、cpuScore最大→latencyMs最小→PeerId最小で
    // 新Hostを決定的に選出する。全Clientが同じロスターを持つため追加の合意メッセージは不要。
    // 戻り値: 候補が無ければ id==0 の PeerInfo。
    PeerInfo electNewHost() const;
    bool promoteToHost();
    // endpoint へClientとして接続する。移行時の再接続経路はこの関数に一元化する。
    // TODO(NAT越え): 将来STUN/リレー対応する場合はこの関数の中だけを差し替える。
    bool connectToEndpoint(const PeerEndpoint& endpoint);

    ENetHost* m_host = nullptr;
    NetworkRole m_role = NetworkRole::Offline;
    std::vector<ENetPeer*> m_peers; // Host視点: 接続中の全Client。Client視点: Hostのみ1件
    bool m_enetInitialized = false;

    PeerId m_localPeerId = 0;
    uint32_t m_nextPeerId = 2; // Host専用。1はHost自身
    std::vector<PeerInfo> m_roster;
    std::unordered_map<ENetPeer*, PeerId> m_peerIds; // Host専用: 接続Peer→PeerId
    float m_localCpuScore = 0.0f;
    uint16_t m_listenPort = 0;
    float m_resourceReportTimer = 0.0f;
    float m_rosterBroadcastTimer = 0.0f;
    bool m_rosterDirty = false;

    MigrationState m_migrationState = MigrationState::None;
    PeerEndpoint   m_migrationTarget;       // 選出された新Hostの接続先(自分が勝者の場合は未使用)
    float          m_migrationRetryTimer  = 0.0f; // 次の接続試行までの待ち
    float          m_migrationElapsed     = 0.0f; // 移行開始からの総経過(タイムアウト判定)
};
