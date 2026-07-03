# PathfindingService

`include/Instances/PathfindingService.hpp`

System直下に自動生成される独立したサービス。Workspace内のTerrainボクセル＋静的`BaseCube`ジオメトリからDetourナビメッシュを構築し、2点間の経路をウェイポイント配列(`Vector3` + Walk/Jump)として返す。ナビメッシュ構築の実体は `Pathfinding::NavMesh`/`NavMeshBuilder`（`doc/Pathfinding/NavMeshBuilder.md`参照、本ドキュメントでは重複説明しない）。

## 継承
`Instance` → `PathfindingService`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `AgentRadius` | `float` | エージェント半径（[0.1,10]にクランプ、既定1.0） |
| `AgentHeight` | `float` | エージェント高さ（[0.5,20]、既定5.0） |
| `AgentMaxClimb` | `float` | 登れる段差高さ（[0,10]、既定0.6） |
| `AgentMaxSlope` | `float` | 歩行可能な最大斜度(度)（[0,89]、既定50.0） |
| `MaxJumpDistance` | `float` | ジャンプ可能な最大水平距離（[0,50]、既定6.0） |
| `MaxJumpHeight` | `float` | ジャンプ可能な最大高低差（[0,50]、既定4.0） |
| `ScenePath` | `string` | シーンファイルパス。`SceneRuntime::loadAndBind`が読込のたびに設定。ディスクキャッシュファイルパス算出に使用（未設定ならディスクキャッシュしない） |
| `m_cache`（private） | `unordered_map<string, unique_ptr<NavMesh>>` | Workspace名（ポインタではない）でキーイングしたナビメッシュキャッシュ |
| `s_active`（private static） | `PathfindingService*` | 現在アクティブなインスタンスへの参照 |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"PathfindingService"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` 経由でAgent系プロパティをYAMLデシリアライズ |
| `FindPath(workspace, start, goal)` | Workspace名でキャッシュを引き、未キャッシュなら現在のAgent系プロパティでナビメッシュを構築してからキャッシュ。start/goalに最も近い歩行可能地点間の経路を返す |
| `Invalidate(workspace)` | 指定Workspaceのキャッシュを破棄（次回`FindPath`で再構築） |
| `InvalidateActive(workspace)`（static） | Instanceツリーを辿れない箇所（`TerrainStreamer`等）から、アクティブなPathfindingServiceのキャッシュを無効化するヘルパー |

## フロー — パス要求

```
FindPath(workspace, start, goal)
  → m_cache に workspace->Name のエントリがあるか確認
      なければ: BuildSettings に Agent系プロパティをコピー
                → cacheFilePathFor(ScenePath, workspace->Name) でディスクキャッシュパス算出
                → NavMesh::Build(workspace, settings, cachePath) で構築しキャッシュへ格納
  → キャッシュされた NavMesh::FindPath(start, goal) を呼び出し PathWaypoint配列 を返す

地形編集時 (TerrainStreamer::applyBrush等):
  → InvalidateActive(workspace) 経由で該当Workspaceのキャッシュを破棄
  → 次回 FindPath で再構築される
```

キャッシュはWorkspaceポインタではなく名前でキーイングする。Workspaceは Play/Stop のたびに破棄・再生成されポインタが再利用されうるため、ポインタキーだと誤ヒットの恐れがある。

## 依存関係

- `Pathfinding::NavMesh` / `Pathfinding::NavMeshBuilder`（[NavMeshBuilder](../Pathfinding/NavMeshBuilder.md)参照）
- `Workspace`, `PropertyRegistry`
- recastnavigation (Detour)

## 継承クラス

なし
