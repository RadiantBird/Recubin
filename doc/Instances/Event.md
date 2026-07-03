# Event

`include/Instances/Event.hpp`

ツリーに配置してLuauから任意に発火できるカスタムイベント（シグナル）インスタンス。`fire()` 呼び出しで、`Until`系接続で登録された `RCBNScriptConnection`（`Core/RCBNScriptSignal.hpp`）を一括切断する。実際のリスナー管理・引数受け渡しはLuauバインディング層（LuauEngine_Dispatch側）がEvent固有のシグナルを介して行う。

## 継承
`Instance` → `Event`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_untilConnections` | `vector<weak_ptr<RCBNScriptConnection>>` | `fire()` 時に切断される接続一覧（`Until`ヘルパー等が登録） |

## メソッド

| メソッド | 説明 |
|---|---|
| `fire()` | `m_untilConnections` の有効な接続を全て `disconnect()` してからクリア |
| `addUntilConnection(conn)` | 監視対象の接続を追加登録 |
| `getClassName()` | `"Event"` を返す |
| `IsA(name)` | 継承チェーンを含む型チェック |

## 依存関係

- `RCBNScriptSignal` / `RCBNScriptConnection`（`Core/RCBNScriptSignal.hpp`、`weak_ptr`で弱参照保持しライフタイム管理を委ねる）

## 継承クラス

なし
