# Recubin ― 機能紹介

**Powering imagination again.**
C++23 製、Windows向け自作ゲームエンジン。物理・スクリプト・エディターを統合し、
「誰でも簡単にゲームを作ってローカルで公開できる」ことを目指す。

詳細なクラスリファレンスは [README.md](README.md) を参照。本書は機能の全体像を俯瞰するためのまとめ。

---

## 1. コンセプト

- 誰でも簡単にゲームを開発し、ローカルで公開できるように
- Robloxでできなかったことを実現する
- わかりやすい、直感的なエディターを作る
- 物理で遊べるようにする

---

## 2. ビジュアル・レンダリング

- OpenGL による 3D レンダリングパイプライン（[Renderer](Core/Renderer.md)）
- 太陽・月・平行光源によるシーンライティング + シャドウパス（[Lighting](Instances/Lighting.md), [Sun](Instances/Sun.md)/[Moon](Instances/Moon.md)）
- 点光源・スポットライト（[PointLight](Instances/PointLight.md), [SpotLight](Instances/SpotLight.md)）
- ポストプロセスエフェクト（[PostEffect](Instances/PostEffect.md)）
  - CRT / Posterization / Pixelize / **Saturation**（白黒〜反転） / **VHS**（ノイズ） / **ChromaticAberration**
  - `ZIndex` で多段合成、`Intensity` でブレンド比率を制御
- GLB メッシュインポート（`cgltf`）、画像テクスチャ（`stb_image`）
- スカイボックス、面ごとのデカール/テクスチャタイリング

## 3. 物理エンジン（PhysX）

- 剛体シミュレーション・レイキャスト（[Physics](Core/Physics.md)）
- 豊富なプリミティブ：Cube / Sphere / Cylinder / TriangularPrism / MeshCube
- **LiquidCube** による浮力ボリューム（水・液体表現）
- 物理制約ファミリー
  - [Motor](Instances/Motor.md)：回転駆動（`PxRevoluteJoint`）
  - [Weld](Instances/Weld.md)：剛体結合
  - [Rope](Instances/Rope.md)：バネ付き距離拘束
  - [Rod](Instances/Rod.md)：固定長距離拘束
  - ツリー上のどこに置いても自動で有効化される（必要プロパティが揃い次第初期化）

## 4. 地形システム

- ボクセルベースの地形（[Terrain](Core/Terrain.md) / [TerrainStreamer](Core/TerrainStreamer.md)）
- チャンク単位の**非同期ストリーミング**（ワーカースレッドでロード/生成/保存、メインスレッドは結果を受け取るだけ）
- Perlin ノイズ生成 + 手動編集用ブラシ（Raise / Lower / Smooth）
- ボクセル DDA レイキャストで編集直後でも正確なブラシヒット判定（物理エンジンの更新遅延を回避）
- Ramp / Wedge 等の斜面形状に自動分類、リージョン単位で YAML 保存（RLE圧縮）

## 5. キャラクター & AI

- [Humanoid](Instances/Humanoid.md) キャラクターコントローラー
  - 歩行・ジャンプ・接地判定・歩行アニメーション
  - ヘルス管理・死亡演出（ラグドール）
  - 一人称/三人称切替（一人称時は自動で身体を非表示化）
- [PathfindingService](Instances/PathfindingService.md) によるナビメッシュ AI
  - Recast/Detour ベースのナビメッシュ自動生成（地形 + 静的オブジェクトから構築、[NavMeshBuilder](Pathfinding/NavMeshBuilder.md)）
  - ジャンプ経路（Jump Link）にも対応した経路探索
  - Workspace 単位でキャッシュ、地形編集時に自動無効化

## 6. スクリプティング（Luau）

- Roblox 譲りの [Luau](Core/LuauEngine.md) VM を統合、コルーチンベースの `wait()` に対応
- Instance ツリーをそのまま Luau に公開（`instance.Position`、`game:add(className)` 等）
- [PropertyRegistry](Core/PropertyRegistry.md)：プロパティを1行宣言するだけで Luau / YAML / エディター / clone / Undo に自動配線
- カスタムイベント/シグナル（[Event](Instances/Event.md), `RCBNScriptSignal`）
- Script のプロパティ（Enabled / Aborted / Source / Restart）をエディターとLuau双方から操作可能
- **安全対策を標準搭載**（[System](Instances/System.md)）
  - 1フレームでの過剰な Clone / Restart を検知して強制停止（無限増殖対策）
  - スクリプトループのタイムアウト検知
  - 原因スクリプトを種類別に集計してエラー出力

## 7. シーン & オブジェクトシステム

- Instance を頂点とするツリー構造（親子関係・所有権・再帰削除）— [Instance階層](Instances/README.md)
- 複数 Workspace 切り替え対応
- GUI システム（画面空間 / ワールド空間の両対応）
  - ScreenGuiObject 系：TextLabel / TextButton / ImageLabel / ImageButton
  - WorldGuiObject 系：BillboardGui（カメラ追従）/ SurfaceGui（面へベイク）/ ProximityPrompt（インタラクト促進 UI）
- YAML ベースのシーン保存/読込（[SceneLoader](Core/SceneLoader.md)）
- アセット参照は [FileRef](Instances/FileRef.md) 経由で解決し、パス破損を防止
- キーフレームアニメーション（[Animation](Instances/Animation.md)）
- 空間オーディオ（[AudioService](Core/AudioService.md)、`miniaudio`）
- カメラのプログラマブル制御（`ControlMode::Program`、Luau から操作可能）

## 8. エディター

ImGui + ImGuizmo ベースの統合エディター（[EditorManager](Editor/EditorManager.md)）

| パネル | 機能 |
|---|---|
| Scene Hierarchy | ツリー表示・選択 |
| Properties | プロパティインスペクタ（PropertyRegistry 駆動） |
| Viewport | 3D 編集ビュー + ギズモ操作 |
| Secondary Viewport | 補助ビューポート |
| Content Browser | アセットファイルブラウザ |
| Console | C++ / Luau ログ統合コンソール |
| Animation Editor | キーフレームアニメーション編集 |
| Command History | Undo / Redo |

- 地形ブラシ、Insert Object によるインスタンス配置
- 新規クラスは自動的にエディターへ公開（PropertyRegistry 経由）

## 9. データ・アセット管理

- YAML（yaml-cpp）によるシーン・地形データの保存
- `Packager` によるアセットパッケージング
- `AssetGuard` / `FileRef` によるパス安全性の確保
- ログシステム（コンソール・ファイル双方に出力）

---

## 使用技術一覧

| カテゴリ | 技術 |
|---|---|
| 言語 | C++23, Luau |
| グラフィックス | OpenGL（GLFW / GLEW） |
| 物理 | PhysX |
| UI | ImGui, ImGuizmo |
| オーディオ | miniaudio |
| データ | YAML-cpp |
| アセット | stb_image, cgltf |
| AI / ナビゲーション | recastnavigation |
| ビルド | Python カスタムビルドシステム |

## 今後の展望

- Mac 対応（レンダラー抽象化 → OpenGL 4.1 準拠実装、ファイル操作のインターフェイス化）
- DirectX（Windows最適化）/ Vulkan（Linux等）への対応拡大
- **Luar 言語**：Rust製 Luau 代替コンパイラ（実験段階、詳細は [LPL.md](LPL.md)）

---
*詳細は各クラスドキュメント（[README.md](README.md)）を参照。*
