# Terrain

`include/Core/Terrain.hpp`

ボクセルベースの地形を表す `Instance` 派生クラス。実データ管理・非同期I/O・メッシュ/物理生成は `TerrainStreamer` に委譲し、`Terrain` 自身は YAML プロパティ（`DataPath` / `Seed` / `Flat` / `Enabled`）の保持と streamer のライフサイクル管理のみを担当する。同ファイル（`src/Core/Terrain.cpp`）にはチャンクの描画メッシュ・物理形状を生成するフリー関数 `buildChunkMesh` / `buildChunkPhysics` も実装されている。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Enabled` | `bool` | 地形の有効/無効（無効化で streamer を破棄） |
| `DataPath` | `std::string` | 地形データ（リージョンファイル）の保存先ディレクトリ |
| `Seed` | `int` | ノイズ生成シード |
| `Flat` | `bool` | true なら平坦地形を生成 |
| `streamer` | `unique_ptr<TerrainStreamer>` | チャンクのストリーミング・生成・保存を担当する実体 |
| `m_appliedDataPath` | `std::string`（private） | 直近 streamer に適用済みの `DataPath`（変更検出用） |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"Terrain"` を返す |
| `IsA(className)` | クラス階層判定 |
| `setProperty(name, value)` | `Enabled` / `DataPath` / `Seed` / `Flat` を YAML から読み込む（未該当は `Instance::setProperty` に委譲） |
| `update(centerPos)` | 毎フレーム呼ぶ。`Enabled=false` なら streamer を破棄、`true` なら未生成時に streamer を構築し `DataPath` 変更を検知して切替え、`streamer->update()` を呼ぶ |

## 関連フリー関数（BlockShape/Chunk 関連）

| 関数 | 説明 |
|---|---|
| `buildChunkMesh(chunk, streamer)` | チャンクの描画用 VAO/VBO/EBO を生成。同時に `physVerts`/`physIndices`/`physConvexBlocks` を埋める。形状（`Cube` / `Wedge_*` / `Tetra_*` / `Ramp_*`）ごとに面カリング・法線計算を行う |
| `buildChunkPhysics(chunk, physics)` | `buildChunkMesh` が埋めた頂点データから PhysX の `PxTriangleMesh`（Cube 面）と `PxConvexMesh`（Ramp/上部Wedge、単位形状をキャッシュしブロック位置へ配置）を生成し `PxRigidStatic` として登録 |

`BlockShape` / `BlockMaterial` / `Block` / `TerrainVertex` / `TerrainMesh` / `Chunk` / `ConvexBlock` は同ヘッダで定義される値/レイアウト構造体（詳細は `TerrainStreamer.md` 参照）。

## Enabled 切替フロー

```
update(centerPos)
  Enabled == false:
    streamer->clear(); streamer.reset();
  Enabled == true:
    streamer 未生成 → new TerrainStreamer(ws, this, DataPath, Seed, Flat)
    DataPath 変更   → streamer->setDataPath(DataPath)
    streamer->update(centerPos)
```

## 依存関係

- `TerrainStreamer`（実データ管理）
- `Instances/Workspace`（`findFirstAncestorWorkspace()` 経由で取得）
- OpenGL（GLEW）, PhysX（`buildChunkMesh`/`buildChunkPhysics` 経由）

## 使われる場所

- `Workspace` に子として配置され、`Workspace` の毎フレーム更新から `update(playerPos)` が呼ばれる
- Luau 側の Terrain 部分編集 API（`terrain_set_block_closure` 等、`LuauEngine`）が `streamer` 経由で操作する
- エディターの地形ブラシ機能が `streamer->applyBrush()` を呼ぶ
