# Recubin Engine — クラスドキュメント

C++23 製 3D ゲームエンジンエディタ。OpenGL レンダリング・PhysX 物理・Luau スクリプト・ImGui エディタ UI を統合。

---

## ディレクトリ構成

| ディレクトリ | 内容 |
|---|---|
| [Math/](Math/) | 数学基盤（Vector3, Quaternion, CFrame, Matrix4） |
| [Util/](Util/) | ユーティリティ（Color4, Material, Logger） |
| [Instances/](Instances/) | シーンオブジェクト階層（Instance ツリー） |
| [Core/](Core/) | エンジンコアシステム（Physics, Renderer, LuauEngine 等） |
| [Editor/](Editor/) | ImGui エディタ UI パネル群 |
| [Pathfinding/](Pathfinding/) | recastnavigation ベースのナビメッシュ生成・経路探索 |

---

## アーキテクチャ概要

```
main.cpp
  ├─ Renderer          ← 3D + ImGui 描画（GUI 描画は Renderer_GUI.cpp に分割）
  ├─ Physics           ← PhysX シミュレーション
  ├─ LuauEngine        ← スクリプト実行（Dispatch/Math は別翻訳単位に分割）
  │    └─ PropertyRegistry ← クラス別プロパティ定義を一元管理し Dispatch に配線
  ├─ AudioService      ← 空間オーディオ
  ├─ User              ← カメラ・キャラクター制御（Humanoid に移動/ジャンプを委譲）
  ├─ EditorManager      ← ImGui エディタパネル群（Editor/）
  └─ Workspace         ← シーンルート（Instance ツリー、詳細は Instances/README.md）
       ├─ Cube / Cylinder / Sphere / ... (BaseCube → Spatial → Instance)
       ├─ Terrain                ← ボクセル地形（実体は TerrainStreamer が非同期ストリーミング）
       ├─ PathfindingService     ← Terrain/BaseCube から NavMeshBuilder 経由でナビメッシュ構築
       ├─ Script
       ├─ System / Humanoid / Motor・Weld・Rope・Rod（物理制約）
       ├─ ScreenGuiObject / WorldGuiObject 系（GUI）
       ├─ Sound
       ├─ Model
       └─ Decal
```

より詳細なクラス構成は各ディレクトリの README（[Instances/](Instances/README.md) の継承ツリー等）を参照。

## 依存関係グラフ（外部ライブラリ）

| ライブラリ | 用途 |
|---|---|
| GLFW / GLEW | OpenGL コンテキスト・入力 |
| ImGui + ImGuizmo | エディタ UI・ギズモ操作 |
| PhysX | 剛体物理シミュレーション |
| Luau | スクリプティング VM |
| miniaudio | オーディオ再生 |
| YAML-cpp | シーンファイルの読み書き |
| stb_image | テクスチャ画像読み込み |

## フレームループ概要

```
毎フレーム:
  1. SystemState 同期 (isPlaying / isPaused)
  2. Play/Stop トランジション処理
  3. [Play 中] physics->update()
  4. [Play 中] luauEngine->update() → スクリプト再開
  5. [Play 中] luauEngine->executeWorkspaceScripts()
  6. user->processInput()（Humanoid 経由で移動・ジャンプ・接地判定）
  7. terrainStreamer->update(playerPos) → チャンクの非同期ロード/アンロード
  8. renderer->render() → 3D シーン + ImGui（GUI パネル・ワールド/スクリーン GUI 含む）
  9. audioService->updateSounds()
```

## プロパティ解決フロー（PropertyRegistry）

新規プロパティは `PropertyRegistry::registerClass()` に 1 行宣言するだけで、Luau / YAML / エディター / clone / Undo の全経路に配線される（詳細: [PropertyRegistry](Core/PropertyRegistry.md)）。ただし `BaseCube`/`Cube` など一部クラスは特殊挙動が多いため意図的に手書き `setProperty` を維持している。

```
PropertyDesc 宣言 (field/fieldVia/method_prop/enumProp/sig)
  → registerClass(className, props)
      ├─ LuauEngine::InitDispatchTable_* → applyToDispatch() → DispatchTable/SetterTable に配線
      ├─ SceneLoader::loadProperty() → YAML 読込
      ├─ 保存処理::saveProperties() → YAML 書出
      ├─ Instance::clone() → cloneFields()
      └─ PropertiesPanel → collectSchema() でエディター表示
```

## 地形ストリーミング & パスファインディング フロー

```
Terrain::update()
  → TerrainStreamer::update(playerPos)
      1. プレイヤー周辺 STREAM_RADIUS チャンクを非同期ロード（ワーカースレッド）
      2. 範囲外チャンクをアンロード（変更があれば YAML へ保存）
      3. ロード結果をメインスレッドへ反映、dirty チャンクを再メッシュ・再物理化
      4. 地形編集(applyBrush等) → PathfindingService::InvalidateActive() でナビメッシュキャッシュ破棄

PathfindingService::FindPath(workspace, start, goal)
  → Workspace 名でキャッシュ確認
      未キャッシュなら NavMeshBuilder が Terrain ボクセル + 静的 BaseCube から
      Recast/Detour ナビメッシュを構築（詳細: [NavMeshBuilder](Pathfinding/NavMeshBuilder.md)）
  → キャッシュ済み NavMesh::FindPath() で Walk/Jump ウェイポイント配列を返す
```
