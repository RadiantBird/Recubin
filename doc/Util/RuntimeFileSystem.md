# RuntimeFileSystem

`include/Util/RuntimeFileSystem.hpp` / `src/Util/RuntimeFileSystem.cpp`

実行時のファイルI/Oの唯一の窓口。Luauの`IO.*` APIと`TextFile`の両方がこのクラスを通る。

## 生成と受け渡し

| 場所 | 内容 |
|---|---|
| `src/main.cpp`（エディター） | シーン束縛時に生成し`LuauEngine::setRuntimeFileSystem()`へ渡す。`EditorManager`（`CodeEditorPanel`）にも同じものを渡す |
| `src/game_main.cpp`（ランタイム） | 起動時に生成し`LuauEngine::setRuntimeFileSystem()`へ渡す |

コンストラクタ引数は`externalAllowed`（既定`false`）と`root`（既定は空）。rootが空ならカレントディレクトリ
を絶対化・正規化した「ポータブルroot」になる。起動場所が違えばrootも違うため、エディターと配布ランタイムの
保存領域は分離される。`externalAllowed`は`checkedFileSystem()`が`System.EnableExternalFileAccess`から毎回更新する。

## パス解決（`resolve()`）

- 空パスは拒否する。
- 絶対パスは`externalAllowed`が`true`のときだけ許可する（root制約は適用されない）。
- 相対パスは`..`を含むと拒否し、`weakly_canonical`後にroot配下でなければ拒否する（symlink脱出対策）。
- 読み取り系は対象が存在しないとエラー（`path does not exist`）。書き込み系は存在しなくてもよい。
- 結果は`RuntimeFileResult`（`success`/`value`/`error`/`exists`/`isFile`/`isDirectory`/`size`）。
  例外は使わない。

## 操作

| メソッド | 動作 |
|---|---|
| `read` | 通常ファイルの全文を返す。128 MiB超は失敗 |
| `write` | 下記のアトミック書き込み。親ディレクトリは自動作成 |
| `append` | `read`した内容に連結して`write`し直す（存在しなければ新規） |
| `exists`/`isFile`/`isDirectory` | 状態を返す。存在しないこと自体はエラーにならない |
| `list` | ディレクトリ直下を名前順で返す。`path`はroot相対 |
| `createDirectory` | 再帰的に作成 |
| `copy`/`move` | `overwrite`指定で上書き。`move`の上書きは先に宛先を削除する |
| `remove` | ファイル、または空ディレクトリ。rootは削除不可 |
| `removeTree` | 再帰削除。root・ホーム（`HOME`/`USERPROFILE`）・ドライブ/FS rootは保護 |
| `readTextFile`/`writeTextFile` | `TextFile`用（下記） |

### アトミック書き込み（`write`）

1. `<path>.tmp`へ全データを書く（失敗したら`.tmp`を削除）。
2. 既存ファイルがあれば`<path>.backup-<hash>`へ退避する。
3. `.tmp`を本来のパスへrenameする。
4. 成功したらbackupを削除。renameに失敗したらbackupから復元する。

データサイズの上限は128 MiB（`MAX_FILE_SIZE`）。

## 利用元1: Luau `IO.*`

`System.EnableIOAPI`が`true`のときだけLuauへ公開される（`LuauEngine.cpp`の`checkedFileSystem()`）。
`ReadText`/`ReadBytes`/`WriteText`/`WriteBytes`/`AppendText`/`AppendBytes`/`Exists`/`IsFile`/
`IsDirectory`/`List`/`CreateDirectory`/`Copy`/`Move`/`Remove`/`RemoveTree`。
読取りは値または状態、変更系の成功は`true`、失敗はLuaエラー。
`EnableIOAPI`/`EnableIPCAPI`/`EnableExternalFileAccess`はエディターでのみ変更でき、Luauからは読み取り専用。

## 利用元2: TextFile

`TextFile`は`IO.*`の権限なしで使える。

```
readTextFile(seedPath, storageId)
  storageIdがUUIDでなければ失敗
  textfiles/<storageId>.txt（overlay）が存在 → それを返す
  存在しない → AssetGuard::allow(seedPath)を確認 → seedを読む
            → overlayへwriteしてから内容を返す

writeTextFile(storageId, content)
  overlay（textfiles/<storageId>.txt）へwriteするだけ。seedは変更しない
```

- `ContentPath`（seed）は配布時の初期内容、`StorageId`はmutable copyを識別するUUID。
- Luauの`file.Content`の読み書きが上の2つに対応する（`LuauEngine_Dispatch.cpp`）。
- エディターの`CodeEditorPanel`は保存時に`writeTextFile`を呼ぶ。
- `clone()`は新しい`StorageId`を割り当てるため、overlayは複製元と別物になる。
- `IO.*`からもroot相対の`textfiles/...`には触れる。

詳細は[TextFile](../Instances/TextFile.md)を参照。

## IPC

`EnableIPCAPI`のグローバル`IPC.Connect/Send/Receive/Close`はstubのままで、未実装エラーを返す。
実際のプロセス間通信は[Program](../Instances/Program.md)インスタンスが担う（`EnableIPCAPI`が必要）。
