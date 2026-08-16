# SceneLoader

`include/Core/SceneLoader.hpp`

YAML シーンファイルを読み込んで Instance ツリーを再構築する静的ファクトリクラス。

## メソッド（すべて static）

| メソッド | 説明 |
|---|---|
| `loadScene(filePath)` | YAML ファイルをパースしてルート Instance を返す |
| `loadSceneResult(filePath)` | `LoadResult`（状態、メッセージ、`SceneDocumentMetadata`）付きで読み込む。ヘッダーなしはscene version 0 |
| `saveSceneResult(root, filePath, metadata)` | SceneヘッダーとCharacter Animation参照移行versionを保存する。失敗はfalse |
| `parseInstance(node)` | YAML ノードを再帰的に Instance に変換 |
| `createInstance(className)` | クラス名文字列から Instance を new して返すファクトリ |

## 対応クラス（createInstance）

`Instance`, `Spatial`, `BaseCube`, `Cube`, `Script`, `Sound`, `Decal`, `Model`, `Workspace`

## シーンロードフロー

```
SceneLoader::loadScene("assets/scenes/test_scene.yaml")
  → YAML ドキュメントをパース
  → parseInstance(rootNode)
       → createInstance(className)   ← new でオブジェクト生成
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

- `main.cpp` 起動時とゲーム停止時（シーンリロード）に呼ばれる

## 保存ヘッダーと移行状態

Scene YAMLには`recubin: {type: scene, version: 0}`を付けられる。`version: 1`以上や
`type`がscene以外の文書は、将来形式として明示的に拒否する。旧ファイル（ヘッダーなし）は
暗黙のversion 0として読み込む。新規保存では移行済みの場合だけ
`migrations.character_animation_bindings.version: 1`を出力する。

廃止済みの`migrations.default_r6_animations`と`animations.r6_walk.ContentPath`は旧Scene移行用の
read-only互換データとして読み取るが、新規保存へは出力しない。HumanoidのWalk/Jump/Equip参照は
通常のInstanceプロパティとして保存し、全Tree構築後にAnimation Instanceへ解決する。
