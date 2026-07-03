# NavMesh（NavMeshBuilder.hpp）

`include/Pathfinding/NavMeshBuilder.hpp`

Workspace 内の静的ジオメトリ（Terrain ボクセル + 静的 BaseCube）から recastnavigation (Recast/Detour) を使って構築した Detour ナビメッシュ。`Pathfinding` 名前空間に属し、Workspace ごとにキャッシュして使い回すことを想定する。ジャンプでしか繋がらない境界間は簡易な境界エッジペアリングで OffMeshLink として接続する。

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

## メソッド

| メソッド | 説明 |
|---|---|
| `static Build(workspace, settings, cachePath)` | ナビメッシュを構築。歩行可能ジオメトリが無い等の失敗時は `nullptr`。`cachePath` 指定時はジオメトリ+設定のハッシュが一致すればディスクキャッシュを読み込み Recast 再構築をスキップ |
| `FindPath(start, goal)` | start/goal に最も近い歩行可能地点間の経路を `PathWaypoint` 配列で返す。見つからなければ空配列 |

## フロー

```
NavMesh::Build(workspace, settings, cachePath)
  1. collectGeometry(workspace)         Terrainチャンクのphys頂点/インデックス + 静止BaseCubeのOBB三角形を収集
  2. computeGeometryHash(verts,tris,settings)  FNV-1aハッシュ
  3. cachePath指定 かつ ハッシュ一致キャッシュあり → loadNavCache() でnavDataを読み込みRecast工程をスキップ
     それ以外:
       rcCreateHeightfield → rcMarkWalkableTriangles → rcRasterizeTriangles
       → rcFilterLedgeSpans等のフィルタ
       → rcBuildCompactHeightfield → rcErodeWalkableArea → rcBuildDistanceField → rcBuildRegions
       → rcBuildContours → rcBuildPolyMesh → rcBuildPolyMeshDetail
       → collectBoundaryEdges(pmesh) + buildJumpLinks()  境界エッジペアからOffMeshLink(ジャンプ接続)を生成
       → dtCreateNavMeshData(params + offMeshCon*)
       → cachePath指定なら saveNavCache()
  4. dtNavMesh::init(navData) → dtNavMeshQuery::init()
  5. NavMesh{m_navMesh, m_navQuery} を返す

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

`NAVCACHE_MAGIC ("RNMC") | version(u32) | geometryHash(u64) | size(i32) | navData` のバイナリ。ハッシュはジオメトリ頂点/インデックス + `BuildSettings` から算出し、地形やビルド設定が変わればキャッシュミスして再構築する。

## 依存関係

- recastnavigation（Recast: `Recast.h`、Detour: `DetourNavMesh.h`, `DetourNavMeshBuilder.h`, `DetourNavMeshQuery.h`, `DetourStatus.h`）
- `Instances/Workspace`, `Instances/BaseCube`
- `Core/Terrain`, `Core/TerrainStreamer`（`Chunk::physVerts` / `physIndices`）
- `Math/CFrame`, `Math/Vector3`
- `Pathfinding/PathTypes.hpp`（`PathWaypoint`, `WaypointAction`）

## 使われる場所

- `Instances/PathfindingService` が `NavMesh::Build()` で Workspace ごとにナビメッシュを構築・キャッシュし、`FindPath()` を経路探索 API として公開する（詳細は [doc/Instances/PathfindingService.md](../Instances/PathfindingService.md)）
