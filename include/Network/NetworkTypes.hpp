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

enum class MessageType : uint8_t {
    Chat          = 0,
    DummyPosition = 1
};

// 将来のホスト自動選出用に予約(このセッションでは未使用・未実装)。
// TODO: 各ノードのCPUスコア・レイテンシを定期計測してRELIABLEチャンネルでHostへ通知し、
//       Host離脱検知時にこの情報を集計して次のHostを自動選出する。
struct PeerResourceInfo {
    float cpuScore  = 0.0f;
    float latencyMs = 0.0f;
};
