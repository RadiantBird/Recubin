# RCBNScriptSignal

`include/Core/RCBNScriptSignal.hpp`

Luau スクリプトへ公開するイベント（シグナル）の実装。`Instance` の各種イベント（`Touched` 等）や `PropertyRegistry::sig<M>()` から利用され、Luau 側の `:Connect()` / `:Once()` / `:Disconnect()` を C++ 側で仲介する。

## 関連クラス

- `Listener` — 1 接続分の情報（`luaRef`, `once`, `id`）を持つ構造体
- `RCBNScriptSignal` — イベント本体。リスナーの登録・発火を管理
- `RCBNScriptConnection` — `:Connect()` が返す接続ハンドル。`weak_ptr<RCBNScriptSignal>` を保持し `disconnect()` で解除する

## メンバ変数（RCBNScriptSignal）

| 変数 | 型 | 説明 |
|---|---|---|
| `m_mainL` | `lua_State*` | 最初に `connect()` された際の `lua_State`（`fire()` の既定呼び出し先） |
| `m_listeners` | `vector<Listener>` | 登録済みリスナー一覧 |
| `m_nextId` | `int` | 次に割り当てる接続 ID |

## メンバ変数（RCBNScriptConnection）

| 変数 | 型 | 説明 |
|---|---|---|
| `m_signal` | `weak_ptr<RCBNScriptSignal>` | 接続元シグナルへの弱参照（シグナル破棄後の解除呼び出しを安全にする） |
| `m_id` | `int` | 対応する `Listener::id` |

## メソッド

| メソッド | 説明 |
|---|---|
| `connect(L, luaRef, once)` | リスナーを追加し ID を返す。初回呼び出し時に `m_mainL` を記録する |
| `disconnect(id)` | 指定 ID のリスナーを削除し、対応する Lua 参照を `lua_unref` する |
| `disconnectAll()` | 全リスナーを解除（`Instance::Destroy()` 時などに使用） |
| `fire(L, pushArgs)` | 登録済みリスナーを（コピーを取ってから）順に `lua_pcall` で呼び出す。`once` なら呼出し後に自動 disconnect。エラーはコンソールにログ出力するのみで伝播させない |
| `fire(pushArgs)` | `m_mainL` を使う簡易オーバーロード |
| `RCBNScriptConnection::disconnect()` | `weak_ptr::lock()` に成功すれば `signal->disconnect(id)` を呼ぶ |

## 発火フロー

```
Lua: signal:Connect(function(...) ... end)
  → RCBNScriptSignal::connect() が luaRef を登録

C++側: signal->fire(L, [](lua_State* L){ /* 引数を積む */ return N; })
  → リスナーをコピーしてイテレート（ハンドラ内 disconnect でも安全）
  → 各リスナーの関数を lua_pcall
  → once なら disconnect
```

## 依存関係

- Luau（`lua_State`, `lua_unref`, `lua_pcall`）
- `LuauEngine::pushSignal` / `pushConnection`（Luau 側ユーザーデータとして公開）

## 使われる場所

- `PropertyRegistry::sig<M>()` が `shared_ptr<RCBNScriptSignal>` メンバを Luau に公開する際に使用
- `Instance` 系クラスのイベント（`Touched`, `ChildAdded` 等）の実装基盤
- `LuauEngine` の `signal_connect_closure` / `signal_once_closure` / `connection_disconnect_closure` から呼ばれる
