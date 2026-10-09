# Script

`include/Instances/Script.hpp`

Luau / Luar スクリプトを保持する Instance。`Workspace` に追加されると `LuauEngine` によって実行される。コルーチンによる `wait()` に対応。

## 継承

`Instance` → `Script`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Source` | `string` | Luau スクリプトのソースコード |
| `Path` | `string` | スクリプトファイルパス |
| `ScriptExt` | `ScriptExtension`（`Luau` / `Luar`） | スクリプトの言語。プロパティ名・YAMLキーは `ScriptExtension`（文字列） |
| `lastWorkspace` | `Workspace*` | 登録済み Workspace のキャッシュ |
| `Enabled` | `bool` | 実行するかどうか |
| `Sleeping` | `bool` | `wait()` で一時停止中 |
| `Completed` | `bool` | 実行が正常終了した |
| `Aborted` | `bool` | エラーまたは強制停止 |
| `SleepTime` | `float` | `wait()` に渡された秒数 |
| `SleepRemaining` | `float` | 残り待機時間 |
| `Coroutine` | `lua_State*` | Luau コルーチンの状態 |

## ScriptExtension

`Luar` のスクリプトは実行時に `LuarCompiler`（`luar_compiler.dll`）で Luau へ変換してからバイトコード化する。実行時の判定はこの値のみで、ファイル名や `Script.Name` は見ない。

- `Path` を設定すると `.luar` → `Luar`、`.luau` / `.lua` → `Luau` に追従する（`.luauc` は変更しない）。YAMLに項目が無い旧シーンも `Path` から決まる。
- Properties で手動変更できる。YAMLでは `ContentPath` の後に保存され、読み込み時は明示値が優先される。
- 新規スクリプト作成ダイアログで拡張子（`.luau` / `.luar`）を選べる。
- `.luauc`（パッケージ済みバイトコード）は `Luar` でも再変換しない。Packager は `.luar` を Luau 化したうえで `.luauc` に出力する。
- `LocalScript` / `ModuleScript` も同じ値を持ち、`clone()` で複製される。

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
