# Instances

シーンオブジェクト階層。すべてのシーン要素は Instance を継承した木構造で管理される。

## 継承ツリー

```
Instance
  ├─ Spatial
  │    ├─ BaseCube
  │    │    ├─ Cube          ← 描画可能なキューブ（最も頻繁に使用）
  │    │    │    └─ Skybox   ← 空を表す巨大キューブ
  │    │    ├─ Cylinder      ← 円柱プリミティブ
  │    │    ├─ Sphere        ← 球プリミティブ
  │    │    │    ├─ Sun      ← 太陽
  │    │    │    └─ Moon     ← 月
  │    │    ├─ TriangularPrism ← 三角柱プリミティブ
  │    │    ├─ MeshCube      ← GLBメッシュ描画キューブ
  │    │    └─ LiquidCube    ← 浮力を与える液体ボリューム
  │    ├─ Sound              ← 3D 空間オーディオ
  │    └─ Model              ← グループコンテナ
  ├─ Script                  ← Luau スクリプト
  ├─ Decal                   ← 面テクスチャ
  ├─ Texture                 ← 面テクスチャ設定（タイル対応）
  ├─ LightSource              ← Point/Spot ライト共通基底
  │    ├─ PointLight         ← 全方位点光源
  │    └─ SpotLight          ← コーン状指向性光源
  ├─ Lighting                ← シーン全体の平行光源設定
  ├─ Workspace               ← シーンルート
  ├─ ScreenGuiObject         ← 画面空間 GUI 基底
  │    ├─ GuiButton          ← クリック可能 GUI 基底
  │    │    ├─ TextButton    ← テキスト付きボタン
  │    │    └─ ImageButton   ← 画像付きボタン
  │    ├─ TextLabel          ← テキスト表示
  │    └─ ImageLabel         ← 画像表示
  ├─ WorldGuiObject          ← ワールド空間 GUI 基底
  │    ├─ BillboardGui       ← 常にカメラを向くパネル
  │    │    └─ ProximityPrompt ← インタラクト促進 UI
  │    └─ SurfaceGui         ← Cube フェイスにベイクされる GUI
  ├─ AppImage                ← アイコン画像パスのみを持つデータホルダー
  ├─ System                  ← シングルトン（Insert Object非対象）
  ├─ Humanoid                ← キャラクターコントローラー
  ├─ Event                   ← カスタムイベント
  ├─ UserInput               ← User.Input（キー/マウス入力）
  ├─ FileRef                 ← アセットパス参照
  ├─ Tool                    ← 装備可能な道具
  ├─ Motor                   ← 物理制約（回転駆動）
  ├─ Weld                    ← 物理制約（剛体結合）
  ├─ Rope                    ← 物理制約（バネ付き距離拘束）
  ├─ Rod                     ← 物理制約（固定長距離拘束）
  ├─ Animation               ← キーフレームアニメーション
  ├─ PostEffect              ← ポストプロセスエフェクト
  └─ PathfindingService      ← ナビメッシュパスファインディング
```

## クラス一覧

| クラス | ファイル | 概要 |
|---|---|---|
| [Instance](Instance.md) | `include/Instances/Instance.hpp` | 基底クラス（名前・親子関係） |
| [Spatial](Spatial.md) | `include/Instances/Spatial.hpp` | 3D トランスフォームを持つ基底 |
| [BaseCube](BaseCube.md) | `include/Instances/BaseCube.hpp` | 物理対応キューブ基底 |
| [Cube](Cube.md) | `include/Instances/Cube.hpp` | 描画可能なキューブ |
| [Skybox](Skybox.md) | `include/Instances/Skybox.hpp` | 空を表す巨大キューブ |
| [Cylinder](Cylinder.md) | `include/Instances/Cylinder.hpp` | 円柱プリミティブ |
| [Sphere](Sphere.md) | `include/Instances/Sphere.hpp` | 球プリミティブ |
| [Sun](Sun.md) | `include/Instances/Sun.hpp` | 太陽 |
| [Moon](Moon.md) | `include/Instances/Moon.hpp` | 月 |
| [TriangularPrism](TriangularPrism.md) | `include/Instances/TriangularPrism.hpp` | 三角柱プリミティブ |
| [MeshCube](MeshCube.md) | `include/Instances/MeshCube.hpp` | GLBメッシュ描画キューブ |
| [LiquidCube](LiquidCube.md) | `include/Instances/LiquidCube.hpp` | 浮力を与える液体ボリューム |
| [Workspace](Workspace.md) | `include/Instances/Workspace.hpp` | シーンルート・登録窓口 |
| [Script](Script.md) | `include/Instances/Script.hpp` | Luau スクリプトコンテナ |
| [Sound](Sound.md) | `include/Instances/Sound.hpp` | 3D 空間オーディオ |
| [Decal](Decal.md) | `include/Instances/Decal.hpp` | 面テクスチャオーバーレイ |
| [Texture](Texture.md) | `include/Instances/Texture.hpp` | 面テクスチャ設定（タイル対応） |
| [LightSource](LightSource.md) | `include/Instances/LightSource.hpp` | Point/Spot ライト共通基底 |
| [PointLight](PointLight.md) | `include/Instances/PointLight.hpp` | 全方位点光源 |
| [SpotLight](SpotLight.md) | `include/Instances/SpotLight.hpp` | コーン状指向性光源 |
| [Lighting](Lighting.md) | `include/Instances/Lighting.hpp` | シーン全体の平行光源設定 |
| [Model](Model.md) | `include/Instances/Model.hpp` | オブジェクトグループ |
| [ScreenGuiObject](ScreenGuiObject.md) | `include/Instances/ScreenGuiObject.hpp` | 画面空間 GUI 基底 |
| [GuiButton](GuiButton.md) | `include/Instances/GuiButton.hpp` | クリック可能 GUI 基底 |
| [TextLabel](TextLabel.md) | `include/Instances/TextLabel.hpp` | テキスト表示 GUI |
| [TextButton](TextButton.md) | `include/Instances/TextButton.hpp` | テキスト付きボタン |
| [ImageLabel](ImageLabel.md) | `include/Instances/ImageLabel.hpp` | 画像表示 GUI |
| [ImageButton](ImageButton.md) | `include/Instances/ImageButton.hpp` | 画像付きボタン |
| [WorldGuiObject](WorldGuiObject.md) | `include/Instances/WorldGuiObject.hpp` | ワールド空間 GUI 基底 |
| [BillboardGui](BillboardGui.md) | `include/Instances/BillboardGui.hpp` | 常にカメラを向くパネル |
| [ProximityPrompt](ProximityPrompt.md) | `include/Instances/ProximityPrompt.hpp` | インタラクト促進 UI |
| [SurfaceGui](SurfaceGui.md) | `include/Instances/SurfaceGui.hpp` | Cube フェイスにベイクされる GUI |
| [AppImage](AppImage.md) | `include/Instances/AppImage.hpp` | アイコン画像パスのみのデータホルダー |
| [System](System.md) | `include/Instances/System.hpp` | シングルトン・安全マージン設定 |
| [Humanoid](Humanoid.md) | `include/Instances/Humanoid.hpp` | キャラクターコントローラー |
| [Event](Event.md) | `include/Instances/Event.hpp` | カスタムイベント |
| [UserInput](UserInput.md) | `include/Instances/UserInput.hpp` | キー/マウス入力(User.Input) |
| [FileRef](FileRef.md) | `include/Instances/FileRef.hpp` | アセットパス参照 |
| [Tool](Tool.md) | `include/Instances/Tool.hpp` | 装備可能な道具 |
| [Motor](Motor.md) | `include/Instances/Motor.hpp` | 物理制約（回転駆動） |
| [Weld](Weld.md) | `include/Instances/Weld.hpp` | 物理制約（剛体結合） |
| [Rope](Rope.md) | `include/Instances/Rope.hpp` | 物理制約（バネ付き距離拘束） |
| [Rod](Rod.md) | `include/Instances/Rod.hpp` | 物理制約（固定長距離拘束） |
| [Animation](Animation.md) | `include/Instances/Animation.hpp` | キーフレームアニメーション |
| [PostEffect](PostEffect.md) | `include/Instances/PostEffect.hpp` | ポストプロセスエフェクト |
| [PathfindingService](PathfindingService.md) | `include/Instances/PathfindingService.hpp` | ナビメッシュパスファインディング |
