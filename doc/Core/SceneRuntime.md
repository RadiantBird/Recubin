# SceneRuntime

`include/Core/SceneRuntime.hpp`

シーンを隔離環境へロードし、成功後にliveランタイムへCommitする名前空間関数群。エディター・ゲームランタイム双方で共通するStage／Commit、既定サービス補完、参照再解決、Luauグローバル登録を1箇所にまとめる。物理エンジンの初期化・Lighting の移行・エディター固有のUndo/Terrain/Physics解放は呼び出し側の責務として残す。

## 関連構造体

```cpp
struct StagedSceneLoad {
    std::shared_ptr<System> system; // NullInputBackendの隔離Userを含む隔離ツリー
    std::shared_ptr<User> user;
    SceneLoader::LoadStatus status;
};

struct Bound {
    std::shared_ptr<Workspace> workspace;               // 先頭の Workspace
    std::vector<std::shared_ptr<Workspace>> workspaces;  // System 直下の全 Workspace
};
```

## メソッド（namespace 関数）

| 関数 | 説明 |
|---|---|
| `stageSceneLoad(scenePath, liveSystem, liveUser)` | liveの保存対象スカラーをbaselineにした隔離System/Userへ一度だけロードする。空パスは新規空シーン、非空の不存在パスは`NotFound` |
| `commitAndBind(staged, system, user, engine, window)` | 成功済みStageをlive System/Userへ移植し、既定サービス、参照、アイコン、Luau globalsを確定する |
| `loadAndBind(scenePath, system, user, engine, window)` | シーンロードからグローバル登録までを一括実行し `Bound` を返す |
| `collectWorkspaces(system)` | `system->children` から `IsA("Workspace")` なインスタンスを収集 |
| `applyAppIcon(window, root)` | ルート直下の `AppImage` からアイコン画像を読み込み `glfwSetWindowIcon()` を呼ぶ |
| `bindStandardGlobals(engine, workspace, system, user)` | `workspace->Name` / `"workspace"` / `"System"` / `"system"` / `"User"` を Luau グローバルとして一括登録し、`engine.setWorkspace()` / `setSystem()` も呼ぶ |

## Stage／Commitフロー

```
stageSceneLoad(scenePath, liveSystem, liveUser)
  1. 隔離Systemとremote扱いの隔離User（NullInputBackend）を生成
  2. 省略プロパティ互換のbaselineとしてliveのSystem/User保存対象値をコピー
  3. LoadContextへ隔離System/Userを登録し、YAMLを一度だけ構築
  4. 失敗時はstatus/messageを返し、liveツリーは変更しない

commitAndBind(staged, system, user, engine, window)
  1. live System/User本体、入力、Signal、Cameraを維持して保存対象値と子を移植
  2. Users内の隔離Userをlive Userへ置換し、欠落したUsers/User/Inventoryを補完してToolを同期
  3. Workspace（空ならLighting付き）、PathfindingService、ChatServiceを補完
  4. Constraint/ObjectValue/Humanoid Animation参照をliveツリー上で再解決
  5. applyAppIcon()とbindStandardGlobals()を実行してBoundを返す
```

`loadAndBind()` は上記2処理を連続実行する起動用の便利関数。Open Sceneは先にStageを成功させ、
その後で旧Undo/Terrain/Physics参照を解放してからCommitするため、YAML、文書種別、version、
プロパティ変換、`User`配下の構築失敗では現在シーンを維持する。

## 依存関係

- `SceneLoader`（YAML デシリアライズ）, `LuauEngine`, `User`
- `Instances/System`, `Instances/Workspace`, `Instances/Lighting`, `Instances/AppImage`, `Instances/PathfindingService`
- `Util/AssetGuard`（アイコン画像パスの許可チェック）
- stb_image, GLFW

## 使われる場所

- `main.cpp` / `game_main.cpp` がシーン起動時（初回ロード・シーン切替）に呼ぶ
