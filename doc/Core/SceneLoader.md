# SceneLoader

`include/Core/SceneLoader.hpp`

YAML シーンファイルを読み込んで Instance ツリーを再構築する静的ファクトリクラス。

## メソッド（すべて static）

| メソッド | 説明 |
|---|---|
| `loadScene(filePath)` | YAML ファイルをパースしてルート Instance を返す |
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
