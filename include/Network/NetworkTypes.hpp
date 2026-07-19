#pragma once
#include <cstdint>

// v2.0 ネットワーク基盤の型定義。
// 実際のホスト自動選出・動的ロール切替アルゴリズムは未実装(NetworkManager側にTODO)。
// ここではRole/チャンネル/メッセージ種別の枠組みだけを定義する。

enum class NetworkRole {
    Offline, // ネットワーク無効(UseNetwork=false、または未接続)
    Host,
    Client
};

// ENetのチャンネルID。RELIABLE(チャット・イベント・将来のホストマイグレーション信号)と
// UNRELIABLE(位置同期等、高頻度データ)を分離する。
enum class NetworkChannel : uint8_t {
    Reliable   = 0,
    Unreliable = 1,
    Count      = 2
};

// ピアの識別子。0=無効。初代Hostが1、以降Helloごとに採番。ホスト移行を跨いで不変。
using PeerId = uint32_t;

// ピアの接続先情報。host は ENetAddress::host と同じ表現。
// listenPort はそのピアがHost昇格時にListenするポート。
// 将来のNAT越え(STUN/リレー)対応時はこの構造体を拡張し、接続経路は
// NetworkManager::connectToEndpoint に一元化する。
struct PeerEndpoint {
    uint32_t host       = 0;
    uint16_t listenPort = 0;
};

struct PeerInfo {
    PeerId       id        = 0;
    PeerEndpoint endpoint;
    float        cpuScore  = 0.0f; // 起動時マイクロベンチマークの結果(大きいほど高性能)
    float        latencyMs = 0.0f; // Hostが peer->roundTripTime で観測
    bool         isHost    = false;
};

enum class MessageType : uint8_t {
    Chat            = 0,
    // 1 は旧DummyPosition(モックデモ)。撤去済み、欠番。
    Hello           = 2, // Client→Host: listenPort+前回PeerId
    Welcome         = 3, // Host→Client: 割当PeerId
    Roster          = 4, // Host→全員: 全PeerInfo+nextPeerId
    ResourceReport  = 5, // Client→Host: cpuScore定期報告
    AvatarState     = 6, // Client→Host (UNRELIABLE): 自キャラRootのpos+quat
    AvatarBatch     = 7, // Host→全員 (UNRELIABLE): 全ピアのアバター姿勢
    WorldMapping    = 8, // Host→全員 (RELIABLE): netId→Workspace相対パス表
    WorldTransforms = 9  // Host→全員 (UNRELIABLE): netIdごとのpos+quat(分割送信)
};

// ホスト移行の進行状態(遷移処理はPhase 2で実装)
enum class MigrationState { None, Electing, Rehosting, Reconnecting };
