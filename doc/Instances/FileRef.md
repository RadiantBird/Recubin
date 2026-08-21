# FileRef

`include/Instances/FileRef.hpp`

ファイル（アセット）への参照を表す軽量インスタンス。パスはエディターで設定されYAML上は `ContentPath` キーとして保存されるため、Packagerに追跡・同梱・書換される。スクリプトは生パスを書かず、このインスタンスを消費プロパティ（例: `Sound.Source`）へ参照として渡す（spec.md「ファイルパスを要求するプロパティ」節）。

## 継承
`Instance` → `PhysicalFileInstance` → `BasicPhysicalFileInstance<FileRefPhysicalFileTag>` → `FileRef`相当（型エイリアス）

`FileRef`と`FontFile`は、中央の`include/Instances/PhysicalFileInstances.def`にある次のような1行から生成される。

```cpp
RCBN_FILE_INSTANCE(FileRef, Generic, Other, "All files (*.*)", "*.*")
```

この定義から互換型と`PhysicalFileInstanceRegistry`のmetadata/factoryが作られ、SceneLoader生成・保存、Luau `Instance.new`と`Path`読取、Hierarchyの挿入／グループ化、PropertiesのBrowse/Clearとファイルフィルターへ共通配線される。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Path` | `string` | `PhysicalFileInstance`が所有する参照先ファイルパス。区切りをUTF-8-safeな保存形式へ正規化し、YAML上は従来どおり`ContentPath`、Luauでは読取専用`Path`として公開する |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | Tagから従来の`"FileRef"`を返す |
| `IsA(name)` | `FileRef`、`PhysicalFileInstance`、`Instance`を含む継承チェーンの型チェック |
| `setProperty(name, value)` | 共通基底から実クラスの`PropertyRegistry`スキーマを使ってYAMLデシリアライズ |
| `clone()` | `BasicPhysicalFileInstance<Tag>`が実クラススキーマを使い、型・Path・Nameを保って複製する |

## 依存関係

- `PhysicalFileInstanceRegistry`（中央`.def`の型情報、factory、挿入カテゴリ、ファイルダイアログfilterを提供）
- `PropertyRegistry`（共通基底の`Path`を`ContentPath`キー、`omitEmpty()`、`luaReadOnly()`として登録）

`ContentPath`以外の状態や独自処理が必要な特殊型はX-macroの引数を増やさず、`PhysicalFileInstance`を通常継承して`PhysicalFileInstanceRegistry::registerType()`から追加プロパティとfactoryを登録する。

## 継承クラス

なし
