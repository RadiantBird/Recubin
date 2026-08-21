# SceneLoader

`include/Core/SceneLoader.hpp`

YAML シーンファイルを読み込んで Instance ツリーを再構築する静的ファクトリクラス。

## メソッド（すべて static）

| メソッド | 説明 |
|---|---|
| `loadScene(filePath)` | YAML ファイルをパースしてルート Instance を返す |
| `loadScene(filePath, context)` | 呼び出し単位の `LoadContext` に登録された既存Instanceへ対象クラスをマージして読み込む |
| `loadSceneResult(filePath)` | `LoadResult`（状態、メッセージ、`SceneDocumentMetadata`）付きで読み込む。ヘッダーなしはscene version 0 |
| `loadSceneResult(filePath, context)` | `LoadContext` を使うResult付きロード |
| `saveSceneResult(root, filePath, metadata)` | SceneヘッダーとCharacter Animation参照移行versionを保存する。失敗はfalse |
| `parseInstance(node, context)` | `LoadContext` を伝播しながらYAMLノードを再帰的にInstanceへ変換（内部API） |
| `createInstance(className)` | クラス名文字列から Instance を new して返すファクトリ |

## LoadContext

`LoadContext` はクラス名と既存Instanceの対応を1回のロード呼び出しだけに適用する。`System`や
`User`のように新規生成せず既存オブジェクトへプロパティと子をマージする対象を
`registerMergeInstance(className, instance)` で登録する。Contextなしのロードには登録が存在せず、
通常クラスだけをファクトリ生成する。登録内容を保持するプロセス全体の静的状態はないため、
失敗後や別ロードへマージ対象が漏れない。

## 対応クラス（createInstance）

`Instance`, `Spatial`, `BaseCube`, `Cube`, `Script`, `Sound`, `Decal`, `Model`, `Workspace`

## シーンロードフロー

```
SceneLoader::loadScene("assets/scenes/test_scene.yaml", context)
  → YAML ドキュメントをパース
  → parseInstance(rootNode)
       → context対象なら既存Instance、その他はcreateInstance(className)
       → node のプロパティを setProperty() で適用
       → 子ノードを再帰的に parseInstance()
       → setParent() で親子関係を構築
            → onAncestorChanged() が伝播
  → Workspace* を返す
```

## 依存関係

- YAML-cpp
- `Instance` 継承ツリー全体

## 使われる場所

- `SceneRuntime` の隔離Stage、および単体のシーン読込処理から呼ばれる

## 保存ヘッダーと移行状態

Scene YAMLには`recubin: {type: scene, version: 0}`を付けられる。`version: 1`以上や
`type`がscene以外の文書は、将来形式として明示的に拒否する。旧ファイル（ヘッダーなし）は
暗黙のversion 0として読み込む。新規保存では移行済みの場合だけ
`migrations.character_animation_bindings.version: 1`を出力する。

廃止済みの`migrations.default_r6_animations`と`animations.r6_walk.ContentPath`は旧Scene移行用の
read-only互換データとして読み取るが、新規保存へは出力しない。HumanoidのWalk/Jump/Equip参照は
通常のInstanceプロパティとして保存し、全Tree構築後にAnimation Instanceへ解決する。
