# Workspace

`include/Instances/Workspace.hpp`

シーンのルートコンテナ。スクリプトと物理オブジェクトの登録窓口を担う。

## 継承

`Instance` → `Workspace`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `physicsEngine` | `Physics*` | リンクされた物理エンジン |
| `pendingInstances` | `vector<Instance*>` | 物理アクター未作成のキューブ待機リスト |
| `scripts` | `vector<Instance*>` | 実行対象の Script オブジェクト一覧 |

## メソッド

| メソッド | 説明 |
|---|---|
| `registerScript(s)` | Script を実行リストに追加 |
| `unregisterScript(s)` | Script を実行リストから削除 |
| `registerCube(c)` | BaseCube を pendingInstances に追加 |
| `setPhysicsEngine(physics)` | Physics を接続 |
| `buildTestSpace()` | テストシーン生成（TODO） |

## フレンドクラス

`Script` と `BaseCube` は Workspace の登録メソッドに直接アクセスできる。

## 相互作用フロー

```
BaseCube::onAncestorChanged()
  → Workspace::registerCube()      → pendingInstances
                                        → Physics::createActor() [次フレーム]

Script::onAncestorChanged()
  → Workspace::registerScript()    → scripts
                                        → LuauEngine::executeWorkspaceScripts()
```

## 依存関係

- `Instance`, `Physics`（前方宣言）

## 使われる場所

- `main.cpp` でシーンロード後に `setPhysicsEngine()` を呼ぶ
- `Renderer`, `LuauEngine`, `EditorManager` がシーンデータのソースとして参照する
