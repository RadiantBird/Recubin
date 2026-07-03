# SceneRuntime

`include/Core/SceneRuntime.hpp`

シーンのロードとランタイムへのバインドを一括で行う名前空間関数群。「シングルトン登録 → シーンロード → デフォルト Workspace/PathfindingService 補完 → アプリアイコン適用 → グローバル変数登録」という、エディター・ゲームランタイム双方で共通する起動手順を1箇所にまとめる。物理エンジンの初期化・Lighting の移行・エディター固有処理は呼び出し側の責務として残す。

## 関連構造体

```cpp
struct Bound {
    std::shared_ptr<Workspace> workspace;               // 先頭の Workspace
    std::vector<std::shared_ptr<Workspace>> workspaces;  // System 直下の全 Workspace
};
```

## メソッド（namespace 関数）

| 関数 | 説明 |
|---|---|
| `loadAndBind(scenePath, system, user, engine, window)` | シーンロードからグローバル登録までを一括実行し `Bound` を返す |
| `collectWorkspaces(system)` | `system->children` から `IsA("Workspace")` なインスタンスを収集 |
| `applyAppIcon(window, root)` | ルート直下の `AppImage` からアイコン画像を読み込み `glfwSetWindowIcon()` を呼ぶ |
| `bindStandardGlobals(engine, workspace, system, user)` | `workspace->Name` / `"workspace"` / `"System"` / `"system"` / `"User"` を Luau グローバルとして一括登録し、`engine.setWorkspace()` / `setSystem()` も呼ぶ |

## loadAndBind のフロー

```
loadAndBind(scenePath, system, user, engine, window)
  1. SceneLoader::registerSingleton("System"/"User") → loadScene() → clearSingletons()
  2. system 直下に "User" が無ければ addChild(user)
  3. collectWorkspaces() が空なら Workspace + Lighting を新規生成
  4. system 直下に "PathfindingService" が無ければ自動生成、ScenePath を更新（NavMeshキャッシュパス算出用）
  5. applyAppIcon(window, system)
  6. bindStandardGlobals(engine, workspace, system, user)
  7. Bound{ workspace, workspaces } を返す
```

## 依存関係

- `SceneLoader`（YAML デシリアライズ）, `LuauEngine`, `User`
- `Instances/System`, `Instances/Workspace`, `Instances/Lighting`, `Instances/AppImage`, `Instances/PathfindingService`
- `Util/AssetGuard`（アイコン画像パスの許可チェック）
- stb_image, GLFW

## 使われる場所

- `main.cpp` / `game_main.cpp` がシーン起動時（初回ロード・シーン切替）に呼ぶ
