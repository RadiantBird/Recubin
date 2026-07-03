# Pathfinding

recastnavigation ライブラリを使ったナビメッシュ生成・経路探索クラス群。

| クラス | ファイル | 概要 |
|---|---|---|
| [NavMeshBuilder (NavMesh)](NavMeshBuilder.md) | `include/Pathfinding/NavMeshBuilder.hpp` | Workspace ジオメトリからの Detour ナビメッシュ構築・経路探索 |

`PathTypes.hpp` に `PathWaypoint` / `WaypointAction` の共通型を定義（`Instances/PathfindingService` と共有）。実際の利用は `Instances/PathfindingService`（Workspaceごとのキャッシュ・プロパティ公開を担当）経由で行う。詳細は [doc/Instances/PathfindingService.md](../Instances/PathfindingService.md) を参照。
