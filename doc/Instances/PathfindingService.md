# PathfindingService

`include/Instances/PathfindingService.hpp`

System直下に自動生成される独立したサービス。Workspace内のTerrainボクセル＋静的`BaseCube`ジオメトリからDetourナビメッシュを構築し、2点間の経路をウェイポイント配列(`Vector3` + Walk/Jump)として返す。キャッシュミス時はバックグラウンドで構築し、同じWorkspaceへの要求を1つの構築へ集約する。ナビメッシュ構築の実体は `Pathfinding::NavMesh`/`NavMeshBuilder`（`doc/Pathfinding/NavMeshBuilder.md`参照、本ドキュメントでは重複説明しない）。

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
| `m_builds`（private） | Workspace別ビルドマップ | 実行中のビルド、進捗、ワーカーを所有する |
| `m_requests`（private） | RequestId別要求マップ | start/goalと要求状態を保持する |
| `m_generations`（private） | Workspace別世代番号 | Invalidate後に古いワーカー結果が採用されることを防ぐ |
| `s_active`（private static） | `PathfindingService*` | 現在アクティブなインスタンスへの参照 |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"PathfindingService"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` 経由でAgent系プロパティをYAMLデシリアライズ |
| `FindPath(workspace, start, goal)` | C++同期互換API。キャッシュ済みNavMeshから経路を返す |
| `RequestFindPath(workspace, start, goal)` | キャッシュヒットなら`Ready`と経路、ミスなら非同期ビルドを開始／共有して`Pending`とRequestIdを返す |
| `PollRequest(requestId, outWaypoints)` | ビルド完了を取り込み、要求の`Pending` / `Ready` / `Failed` / `Cancelled`を返す。終端結果は取得後に破棄 |
| `AbandonRequest(requestId)` | yield不能などで結果を待たない要求だけを破棄し、共有中のビルドは継続する |
| `CancelPending()` | 全ビルドを協調キャンセルし、ワーカー終了を待って待機要求をCancelledにする |
| `IsBuildActive()`（static） | 待機UI向けに、いずれかのナビメッシュを構築中か返す |
| `GetBuildProgress()`（static） | 待機UI向けに、現在の構築進捗を0〜1で返す |
| `Invalidate(workspace)` | キャッシュを破棄して世代番号を進め、実行中の古い結果を無効化する |
| `InvalidateActive(workspace)`（static） | Instanceツリーを辿れない箇所（`TerrainStreamer`等）から、アクティブなPathfindingServiceのキャッシュを無効化するヘルパー |

## フロー — パス要求

```
Luau PathfindingService:FindPath(start, goal)
  → RequestFindPath(workspace, start, goal)
      キャッシュあり: ReadyとPathWaypoint配列を即時返却
      キャッシュなし:
        メインスレッドでGeometrySnapshotを取得
        → 同じWorkspaceの実行中ビルドがあれば共有
        → 無ければバックグラウンドビルド開始
        → PendingとRequestIdを返す
  → 呼び出しLuauコルーチンを一時停止
  → 各フレームPollRequest(requestId)
  → Ready: 従来と同じウェイポイントテーブルでコルーチンを再開
     Failed/Cancelled: 警告を記録し、空配列で再開

ナビメッシュ構築中:
  → 描画、ImGui、GLFWイベント処理を継続
  → エディター／ゲームランタイムに進捗付きモーダルを表示
  → 物理、Heartbeat、通常Script/task、Humanoid、ゲーム入力等の世界更新は一時停止
  → 完了時にモーダルを自動で閉じ、通常更新へ戻る

地形編集時 (TerrainStreamer::applyBrush等):
  → InvalidateActive(workspace) 経由で該当Workspaceの世代を進める
  → 古い世代のビルド結果はキャッシュへ採用しない
  → 待機要求があれば最新ジオメトリから再構築する
```

キャッシュはWorkspaceポインタではなく名前でキーイングする。Workspaceは Play/Stop のたびに破棄・再生成されポインタが再利用されうるため、ポインタキーだと誤ヒットの恐れがある。

`require`などyieldできないコンテキストで初回ビルドが必要になった場合は、メインスレッドを同期構築で停止させず、警告と空配列を返す。

## 依存関係

- `Pathfinding::NavMesh` / `Pathfinding::NavMeshBuilder`（[NavMeshBuilder](../Pathfinding/NavMeshBuilder.md)参照）
- `Workspace`, `PropertyRegistry`
- recastnavigation (Detour)

## 継承クラス

なし
