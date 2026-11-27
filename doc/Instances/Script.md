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
| `IsEmbedded` | `bool` | Scene内にソースを保存するかどうか |
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

## 埋め込みソースと外部編集

`Script`、`LocalScript`、`ModuleScript` は埋め込みまたは外部参照として保存できる。埋め込みの場合、
Scene YAMLは`Embedded: true`と`EmbeddedSource`に状態と`Source`本文を保存し、`ContentPath`は空にする。
旧`Properties.Source`は従来どおりPathの別名であり、埋め込み本文には使用しない。埋め込みでない
Scriptの`ContentPath`と外部ファイル編集は従来どおり維持される。

Code Editorの「外部エディターで開く」は、Recubinストレージルートの
`temp/embedded-scripts/__Recubin_<一意ID>.<luau|luar>`を既定アプリで開く。作業ファイルは排他的に
生成され、Sceneへパスを保存しない。手動Scene保存、Autosave、終了前に作業ファイルの変更をSourceへ
取り込む。Play開始前の同期はしない。内部Code Editorと外部編集の両方が同じ同期区間で変更された場合は
どちらも破棄せず、競合を報告して保存・終了を止める。

同期やScene保存に失敗した場合、作業ファイルは保持される。前回異常終了で残ったファイルは再利用も
削除もせず、新しい一意ファイルを作成する。今回のセッションが作成したファイルは、Scene保存成功後の
正常終了時だけ削除する。

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
