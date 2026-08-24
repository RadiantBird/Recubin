# TerrainStreamer

`include/Core/TerrainStreamer.hpp`

チャンク単位のボクセル地形を非同期ロード/アンロードするストリーマー。毎フレーム `update(playerWorldPos)` を呼ぶだけで、プレイヤー半径 `STREAM_RADIUS` チャンク以内のロードと範囲外のアンロード（変更があればディスクへ書き戻し）を行う。ディスク I/O とノイズ生成はワーカースレッドで実行し、メインスレッドは結果キューを介して受け取る。

## メンバ変数（主要）

| 変数 | 型 | 説明 |
|---|---|---|
| `terrainDir` | `std::string` | 地形データの保存先ディレクトリ（`setDataPath()` 経由でのみ変更） |
| `m_workspace` | `Workspace*` | 所有しない。物理エンジン取得に使用 |
| `m_owner` | `Instance*` | 所有しない。チャンク物理アクターの `userData` に設定する `Terrain` |
| `m_noise` | `PerlinNoise` | 地形生成用ノイズ |
| `m_flat` | `bool` | true なら平坦地形 |
| `m_chunks` | `unordered_map<ChunkKey, ChunkEntry, ChunkKeyHash>` | ロード済みチャンク（`Chunk` 本体・`modified`・`ChunkState`） |
| `m_regions` | `unordered_map<RegionKey, RegionCache, RegionKeyHash>` | リージョン単位の YAML キャッシュ（ワーカースレッド専用） |
| `m_worker` / `m_jobMutex` / `m_jobCv` / `m_jobQueue` | スレッド, ミューテックス, 条件変数, `deque<Job>` | メイン→ワーカーへのジョブキュー基盤 |
| `m_resultMutex` / `m_resultQueue` | ミューテックス, `deque<LoadResult>` | ワーカー→メインへのロード結果キュー |

## 定数

| 定数 | 値 | 説明 |
|---|---|---|
| `STREAM_RADIUS` | 4 | ロード対象とするチャンク半径 |
| `REGION_SIZE` | 32 | 1リージョンあたりのチャンク数（1辺） |
| `BLOCK_STUD_SIZE` | 4.0f | 1ブロックのワールドサイズ（studs） |

## メソッド

| メソッド | 説明 |
|---|---|
| `TerrainStreamer(workspace, owner, dataDir, seed, flat)` | ディレクトリ確保とワーカースレッド起動 |
| `~TerrainStreamer()` | 未処理 Load ジョブを破棄（高速終了）、変更済みチャンクを Save ジョブ投入後 Stop、GL/PhysX リソース解放 |
| `setWorkspace(ws)` | Workspace 切替時に参照を更新 |
| `setDataPath(dir)` | 変更済みチャンクを保存 → メイン側リソース解放 → ワーカーへ `SetDir` ジョブ（旧フラッシュ+リージョンキャッシュクリア+ディレクトリ更新） |
| `regenerate(seed, flat)` | 保存済み地形を破棄し新シード/モードで再生成 |
| `clear()` | 全チャンク解放 |
| `update(playerPos)` | 毎フレーム呼ぶ。範囲内ロード要求・範囲外アンロード・結果キューの取り込み・`rebuildIfDirty()` を行う |
| `getChunk(cx, cy, cz)` | チャンクを直接取得（デバッグ・ブラシ用） |
| `getBlockGlobal(wx, wy, wz)` | ワールドブロック座標からブロックを取得（カリング判定等） |
| `markDirty(cx, cy, cz)` | チャンクを次フレーム再メッシュ・再物理化対象にする |
| `flushRegions()` | キャッシュ済みリージョンをファイルへ書き出す |
| `findSurfaceY(wx, wz)` | 列の表層Y座標（未ロードならノイズから推定） |
| `reclassifyColumnShape(wx, wz, persist)` | 表層ブロック形状を近傍高さから Cube/Wedge/Ramp に再分類。`persist=true` で永続化対象化 |
| `applyBrush(worldPos, radius, mode)` | ブラシ編集。`mode`: +1=Raise, -1=Lower, 0=Smooth |
| `raycastVoxel(origin, dir, maxDist, outHit)` | ブロックデータへのボクセル DDA レイキャスト。物理シーンを介さないため編集直後でも最新地形を参照でき、PhysX の SQ 未更新による貫通を回避する |
| `raycastVoxelFace(origin, dir, maxDist, ...)` | ヒットしたブロック座標と面の軸/符号(法線)を返すレイキャスト。エディターブラシの方向判定に使う |
| `applyDirectionalBrush(worldPos, axis, sign, radius, mode)` | axis/sign(面法線)で指定した方向へ掘る/盛る6方向対応ブラシ。上方向(axis==1,sign==+1)のみ既存`applyBrush`のRamp/Wedge自動スロープを維持し、それ以外の5方向はCube単位 |
| `applyColorBrush(worldPos, axis, sign, radius, r, g, b)` | 形状を変えずに表層ブロックの色だけ書き換えるPaintブラシ |
| `beginDiffCapture()` / `endDiffCapture()` | ブラシストローク中の変更を`VoxelDiffEntry`として記録し、`TerrainBrushStrokeCommand`（`CommandHistory.hpp`）でUndo/Redoできるようにする仕組み |
| `setBlock` / `removeBlock` | ワールドブロック座標で1ブロックを書換え/削除する部分編集API |
| `getChunks()` | ロード済みチャンク一覧（`const&`、Renderer 描画用） |

## 内部実装（private、抜粋）

| メソッド/構造体 | 説明 |
|---|---|
| `ChunkState` | `Loading`（非同期ロード中、描画/参照対象外） / `Ready` |
| `workerLoop()` / `enqueueJob(job)` | ワーカースレッドのメインループとジョブ投入 |
| `decodeChunkFromRegion` / `encodeChunkToRegion` / `flushRegionsToDisk` | リージョン YAML の読み書き（ワーカー専用） |
| `generateRawGrid` | ノイズからの生フィル生成（ワーカー） |
| `releaseChunkResources` / `rebuildIfDirty` | GL/PhysX 解放・メッシュ/物理再構築（メインスレッド） |
| `raiseColumn` / `lowerColumn` | ブラシ・スムージング共通の1段昇降処理 |
| `markChunkEdited` | 再構築対象(`mesh.dirty`)かつ永続化対象(`entry.modified`)としてマーク |
| `getPhysics()` | `Workspace::getPhysicsEngine()` 経由で取得。null なら物理生成をスキップ |

ファイル構造は `terrain/r_{rx}_{rz}.yaml`（1リージョン = 32×32 チャンク × 全 y 層）。ブロックデータはチャンクごとに RLE（ランレングス）圧縮して保存する。

## update() のロード/アンロードフロー

```
update(playerPos)
  1. playerPos をチャンク座標へ変換
  2. 半径 STREAM_RADIUS 以内で未ロードのチャンク → Load ジョブを投入
  3. 半径外の Ready チャンク → modified なら Save ジョブ投入後、m_chunks から削除しリソース解放
  4. m_resultQueue から LoadResult を取り出して m_chunks に反映（generated なら reclassify）
  5. dirty なチャンクを rebuildIfDirty() で再メッシュ・再物理化
```

## 依存関係

- `Terrain`（`Chunk` / `BlockShape` 等の型定義を共有）, `Instances/Workspace`
- `Physics`（`Workspace::getPhysicsEngine()` 経由）, `Instances/PathfindingService`
- `Math/PerlinNoise`
- yaml-cpp, `FileLoader`
- std::thread / mutex / condition_variable（ワーカースレッド基盤）

## 使われる場所

- `Terrain::update()` が毎フレーム `streamer->update()` を呼ぶ
- Luau の Terrain 部分編集 API（`terrain_set_block_closure` 等）が `setBlock` / `removeBlock` / `raycastVoxel` を呼ぶ
- エディターの地形ブラシが `applyBrush()` を呼ぶ（レイキャストはボクセル DDA 版を使用し PhysX SQ 未更新問題を回避）
- `Renderer::renderTerrain()` が `getChunks()` でロード済みチャンクを描画する

## リージョン読込失敗

リージョンYAMLの読込失敗は`loadFailed`として記録し、内容を空データへ置き換えて
保存しない。ワーカーのguarded flushが破損リージョンを検出した場合は書込みを
遮断する。未生成リージョンはファイルを読まず、警告なしで生成可能とする。
回帰用の検証はワーカージョブ経由で同期完了を待機し、メインスレッドから
ワーカー専用cacheを直接操作しない。
