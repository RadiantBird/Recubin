# UserInput

`include/Instances/UserInput.hpp`

`User.Input` として公開される、Roblox の UserInputService 相当のインスタンス。毎フレーム `poll()` で前フレームとの差分を取り、キーボード/マウスボタンの押下・解放を `Pressed`/`Released` シグナルで通知する。キーは `"W"`, `"Space"`, `"MouseButton1"` のような固定文字列で表現される。入力の読み取りは `User` が所有する `IInputBackend` を借用して行う（所有しない）。

## 継承
`Instance` → `UserInput`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Pressed` | `shared_ptr<RCBNScriptSignal>` | 押下時発火。引数はキー名(string) |
| `Released` | `shared_ptr<RCBNScriptSignal>` | 解放時発火。引数はキー名(string) |
| `m_input` | `IInputBackend*` | 入力供給源（借用、所有しない） |
| `m_prevKeyDown` | `vector<uint8_t>` | 前フレームのキー押下状態（キーテーブルと同順） |
| `m_prevMouseDown` | `uint8_t[3]` | 前フレームのマウスボタン押下状態 |

キー/マウスの文字列テーブルは `UserInput.cpp` の匿名namespace内に固定定義（A-Z, 0-9, 方向キー, Escape/Space/Return/Tab/Backspace, Shift/Control/Alt各種, MouseButton1-3）。

## メソッド

| メソッド | 説明 |
|---|---|
| `setBackend(input)` | `IInputBackend` を借用登録 |
| `poll()` | 毎フレーム呼び出し、全キー/マウスボタンを前フレームと比較してPressed/Releasedを発火 |
| `isPressed(key)` | 指定キー/マウスボタン名が現在押されているか判定 |
| `clone()` | 常に `nullptr` を返す（入力バックエンドを借用しているため複製不可） |

## 依存関係

- `IInputBackend`, `InputKey.hpp`（`KeyCode`/`MouseButton`）
- `RCBNScriptSignal`（Pressed/Released）
- `User`（バックエンドの所有者）

## 継承クラス

なし
