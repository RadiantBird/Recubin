# Script

`include/Instances/Script.hpp`

Luau スクリプトを保持する Instance。`Workspace` に追加されると `LuauEngine` によって実行される。コルーチンによる `wait()` に対応。

## 継承

`Instance` → `Script`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Source` | `string` | Luau スクリプトのソースコード |
| `Path` | `string` | スクリプトファイルパス |
| `lastWorkspace` | `Workspace*` | 登録済み Workspace のキャッシュ |
| `Enabled` | `bool` | 実行するかどうか |
| `Sleeping` | `bool` | `wait()` で一時停止中 |
| `Completed` | `bool` | 実行が正常終了した |
| `Aborted` | `bool` | エラーまたは強制停止 |
| `SleepTime` | `float` | `wait()` に渡された秒数 |
| `SleepRemaining` | `float` | 残り待機時間 |
| `Coroutine` | `lua_State*` | Luau コルーチンの状態 |

## wait() の動作フロー

```
スクリプト内で wait(2.0) 呼び出し
  → Sleeping = true, SleepRemaining = 2.0
  → lua_yield() でコルーチンを一時停止

毎フレーム LuauEngine::update(dt):
  → SleepRemaining -= dt
  → 0 以下になったら lua_resume() でコルーチンを再開
```

## 依存関係

- `Instance`, `Workspace`（前方宣言）
- Luau SDK（`lua_State`）

## 使われる場所

- `Workspace::scripts` に保持される
- `LuauEngine` が実行・管理する
