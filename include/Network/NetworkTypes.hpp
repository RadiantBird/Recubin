#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

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

// NAT越えで交換するIPv4 UDP候補。hostはENetAddress::hostと同じ表現を使う。
enum class CandidateType : uint8_t {
    Local         = 0,
    ServerReflexive = 1,
    PeerReflexive = 2
};

struct NetworkCandidate {
    CandidateType type = CandidateType::Local;
    uint32_t host = 0;
    uint16_t port = 0;

    bool operator==(const NetworkCandidate&) const = default;
};

struct NatConfig {
    std::string stunHost;
    uint16_t stunPort = 3478;
    std::string rendezvousHost;
    uint16_t rendezvousPort = 3479;
    uint16_t listenPort = 0;
};

enum class ConnectionState : uint8_t {
    Offline,
    Discovering,
    CreatingRoom,
    JoiningRoom,
    Punching,
    Connecting,
    Connected,
    Migrating,
    Failed
};

enum class ConnectionError : uint8_t {
    None,
    MissingConfig,
    StunTimeout,
    RoomNotFound,
    RoomFull,
    RendezvousTimeout,
    PunchTimeout,
    AdmissionRejected,
    EnetTimeout
};

using AdmissionToken = std::array<uint8_t, 16>;

// ピアの接続先情報。host は ENetAddress::host と同じ表現。
// listenPort はそのピアがHost昇格時にListenするポート。
// candidatesは優先順(Local→ServerReflexive→PeerReflexive)で保持する。
struct PeerEndpoint {
    uint32_t host       = 0;
    uint16_t listenPort = 0;
    std::vector<NetworkCandidate> candidates;
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
    AvatarState     = 6, // Client→Host (UNRELIABLE): 移動入力スナップショット+seq(flatForward/flatRight/targetMoveDir/flags/forwardAxis/rightAxis/seq、flags bit2=jumpRequested)
    AvatarBatch     = 7, // Host→全員 (UNRELIABLE): 全ピアのアバター姿勢(各エントリ: id/pos/quat/linVel/lastProcessedSeq)
    WorldMapping    = 8, // Host→全員 (RELIABLE): netId→Workspace相対パス表
    WorldTransforms = 9, // Host→全員 (UNRELIABLE): netIdごとのpos+quat(分割送信)
    SimulationClock = 10 // Host→全員: 物理tick+accumulator alpha
};

// ホスト移行の進行状態(遷移処理はPhase 2で実装)
enum class MigrationState { None, Electing, Rehosting, Reconnecting };
