# FileRef

`include/Instances/FileRef.hpp`

ファイル（アセット）への参照を表す軽量インスタンス。パスはエディターで設定されYAML上は `ContentPath` キーとして保存されるため、Packagerに追跡・同梱・書換される。スクリプトは生パスを書かず、このインスタンスを消費プロパティ（例: `Sound.Source`）へ参照として渡す（spec.md「ファイルパスを要求するプロパティ」節）。

## 継承
`Instance` → `FileRef`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Path` | `string` | 参照先ファイルパス（YAML上は `ContentPath`、`PropertyRegistry`でLua読取専用として登録） |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"FileRef"` を返す |
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` 経由でYAMLデシリアライズ |
| `clone()` | `PropertyRegistry::cloneFields` でPathを複製した新規インスタンスを返す |

## 依存関係

- `PropertyRegistry`（`Path`フィールドを`"FileRef"`クラスとして`ContentPath`キーで登録、`omitEmpty()`/`luaReadOnly()`指定）

## 継承クラス

なし
