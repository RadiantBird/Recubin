#pragma once
#include <Network/NetworkTypes.hpp>
#include <Math/Vector3.hpp>

#include <enet/enet.h>

#include <memory>
#include <string>
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
    bool connect(const std::string& address, uint16_t port);
    // 接続を閉じてRoleをOfflineへ戻す。
    void shutdown();

    // 毎フレーム呼ぶこと(enet_host_service をノンブロッキングで回す)。Offline中は何もしない。
    void poll();

    NetworkRole getRole() const { return m_role; }
    bool isActive() const { return m_host != nullptr; }
    // モックデモ用: 1件でも接続済みPeerがあるか(接続確立の簡易検出に使う)
    bool hasPeers() const { return !m_peers.empty(); }

    // ---- モックデモ用の送信API ----
    // RELIABLEチャンネルで全Peer(Hostの場合)/Host(Clientの場合)へブロードキャストする。
    void sendChatMessage(const std::string& text);
    // UNRELIABLEチャンネルでダミー座標を送信する。
    void sendDummyPosition(const Vector3& pos);

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // TODO(応用tier、今回未実装):
    //   - 各Peerの PeerResourceInfo (CPUスコア・レイテンシ) を定期的に RELIABLE で報告させ、
    //     Host側で集計する reportResourceScore(float cpuScore, float latencyMs) 相当のAPI。
    //   - Host切断検知(ENET_EVENT_TYPE_DISCONNECT を自身がHostのPeerとして受けた場合)から、
    //     集計済みスコアが最大のゲストへ Server ステートを引き継ぐ checkHostMigration()。
    //   - 上記により Role が切り替わった際、旧Host/旧Clientそれぞれの Script/LocalScript
    //     有効化状態・Peer ID再識別を破綻なく共有するインターフェース。

private:
    NetworkManager() = default;
    ~NetworkManager();

    void handleEvent(const ENetEvent& event);
    void broadcastPacket(ENetPacket* packet, NetworkChannel channel);

    ENetHost* m_host = nullptr;
    NetworkRole m_role = NetworkRole::Offline;
    std::vector<ENetPeer*> m_peers; // Host視点: 接続中の全Client。Client視点: Hostのみ1件
    bool m_enetInitialized = false;
};
