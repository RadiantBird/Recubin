# NavMesh（NavMeshBuilder.hpp）

`include/Pathfinding/NavMeshBuilder.hpp`

Workspace 内の静的ジオメトリ（Terrain ボクセル + 静的 BaseCube）から recastnavigation (Recast/Detour) を使って構築した Detour ナビメッシュ。`Pathfinding` 名前空間に属し、Workspace ごとにキャッシュして使い回すことを想定する。Instance ツリーの読み取りと Recast の構築を分離し、読み取り専用スナップショットから各タイルを並列構築する。ジャンプでしか繋がらない境界間は簡易な境界エッジペアリングで OffMeshLink として接続する。

## BuildSettings 構造体

| 変数 | 型 | 説明 |
|---|---|---|
| `agentRadius` | `float` | エージェント半径 (stud, デフォルト 1.0) |
| `agentHeight` | `float` | エージェント身長 (stud, デフォルト 5.0) |
| `agentMaxClimb` | `float` | 段差として登れる最大高さ (stud, デフォルト 0.6) |
| `agentMaxSlope` | `float` | 歩行可能な最大斜度 (度, デフォルト 50.0) |
| `maxJumpDistance` | `float` | ジャンプでつなげる境界エッジ間の最大水平距離 (stud, デフォルト 6.0) |
| `maxJumpHeight` | `float` | ジャンプでつなげる境界エッジ間の最大高低差 (stud, デフォルト 4.0) |

## メンバ変数（NavMesh）

| 変数 | 型 | 説明 |
|---|---|---|
| `m_navMesh` | `dtNavMesh*` | Detour ナビメッシュ本体 |
| `m_navQuery` | `dtNavMeshQuery*` | 経路探索クエリオブジェクト |

## 非同期構築用の型

| 型 | 説明 |
|---|---|
| `GeometrySnapshot` | メインスレッドで収集した頂点・三角形の読み取り専用入力。取得後のワーカーは Workspace / Instance ツリーへアクセスしない |
| `BuildStage` | `Idle`、キャッシュ検証、タイル構築、最終化、完了、キャンセル、失敗の各段階 |
| `BuildControl` | ステージ、完了／総タイル数、0〜1の進捗と協調キャンセルフラグをアトミックに共有する |

## メソッド

| メソッド | 説明 |
|---|---|
| `static CaptureGeometry(workspace)` | Terrain／静的BaseCubeをメインスレッドで走査し、`GeometrySnapshot`を返す |
| `static BuildSnapshot(snapshot, settings, cachePath, control)` | スナップショットから構築するワーカー向け入口。進捗更新と協調キャンセルに対応 |
| `static Build(workspace, settings, cachePath)` | 同期互換入口。内部でスナップショットを取得して構築する |
| `FindPath(start, goal)` | start/goal に最も近い歩行可能地点間の経路を `PathWaypoint` 配列で返す。見つからなければ空配列 |

## フロー

```
メインスレッド
  1. CaptureGeometry(workspace)
       Terrainチャンクのphys頂点/インデックス + 静止BaseCubeのOBB三角形を収集

バックグラウンド
  2. BuildSnapshot(snapshot, settings, cachePath)
  3. computeGeometryHash(vertices, triangles, settings)
  4. cachePath指定 かつ ハッシュ一致キャッシュあり
       → loadNavCache()で全タイルを読み込み、Recast工程をスキップ
     キャッシュミス:
       各三角形を重なるタイルへ事前分類
       → 原子的な次ジョブ番号でタイルをワーカーへ分配
       → 各ワーカーが専用rcContextでRecast工程とOffMeshLink生成を実行
       → 完了タイル数と進捗をBuildControlへ反映
       → タイル座標順に結果を統合して決定的な出力を維持
       → cachePath指定ならsaveNavCache()
  5. dtNavMeshへタイルを追加 → dtNavMeshQuery::init()
  6. NavMesh{m_navMesh, m_navQuery}を返す

タイルごとのRecast工程
  rcCreateHeightfield → rcMarkWalkableTriangles → rcRasterizeTriangles
  → rcFilterLedgeSpans等のフィルタ
  → rcBuildCompactHeightfield → rcErodeWalkableArea → rcBuildDistanceField → rcBuildRegions
  → rcBuildContours → rcBuildPolyMesh → rcBuildPolyMeshDetail
  → collectBoundaryEdges(pmesh) + buildJumpLinks()
  → dtCreateNavMeshData(params + offMeshCon*)

NavMesh::FindPath(start, goal)
  findNearestPoly(start) / findNearestPoly(goal)
  → findPath()            ポリゴン列
  → findStraightPath()    直線化したウェイポイント列
  → PathWaypoint配列に変換（OFFMESH_CONNECTIONフラグ → WaypointAction::Jump）
```

## ジャンプリンク判定（buildJumpLinks）

境界エッジ（隣接ポリを持たないポリゴン辺）同士のペアについて、以下をすべて満たす場合にジャンプ接続とする。障害物による軌道遮蔽は考慮しない簡易判定。

- 水平距離が `0.3 stud` 以上 `maxJumpDistance` 以内
- 高低差が `maxJumpHeight` 以内
- 互いの法線がおおむね逆向き（向かい合っている）
- 片方の外向き法線ともう一方への方向がおおむね一致（正面にある）

## ディスクキャッシュ形式

バージョン3のタイル形式。`NAVCACHE_MAGIC ("RNMC")`、version、geometryHash、`dtNavMeshParams`、タイル数に続けて各タイルのサイズとデータを保存する。ハッシュは三角形順に依存しないジオメトリ内容と `BuildSettings` から算出し、地形やビルド設定が変わればキャッシュミスして再構築する。キャッシュ更新は一時ファイルへ書き込んだ後に置換する。

## 依存関係

- recastnavigation（Recast: `Recast.h`、Detour: `DetourNavMesh.h`, `DetourNavMeshBuilder.h`, `DetourNavMeshQuery.h`, `DetourStatus.h`）
- `Instances/Workspace`, `Instances/BaseCube`
- `Core/Terrain`, `Core/TerrainStreamer`（`Chunk::physVerts` / `physIndices`）
- `Math/CFrame`, `Math/Vector3`
- `Pathfinding/PathTypes.hpp`（`PathWaypoint`, `WaypointAction`）

## 使われる場所

- `Instances/PathfindingService` が `NavMesh::Build()` で Workspace ごとにナビメッシュを構築・キャッシュし、`FindPath()` を経路探索 API として公開する（詳細は [doc/Instances/PathfindingService.md](../Instances/PathfindingService.md)）
