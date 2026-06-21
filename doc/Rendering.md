# レンダリング処理 調査ドキュメント

現状（調査時点）のエンジンのレンダリングパイプラインを実コードから調査した記録。
`doc/Core/Renderer.md` は古い設計（`renderScene`/シングル FBO 想定）を記述しており、
実装は `ViewportRenderDesc` を介した汎用ビューポート描画 (`renderViewport`) に置き換わっている。

## 関連ファイル

| ファイル | 役割 |
|---|---|
| `include/Core/Renderer.hpp` | `Renderer` クラス定義、`ViewportRenderDesc` |
| `src/Core/Renderer.cpp` | 初期化・シャドウパス・メインパス・地形描画 |
| `src/Core/Renderer_GUI.cpp` | ScreenGui / WorldGui / SurfaceGui / ホットバーの描画 |
| `src/vertex.glsl`, `src/fragment.glsl` | メインシェーダー（Phong + シャドウ + トライプラナー） |
| `src/depth_vertex.glsl`, `src/depth_fragment.glsl` | シャドウマップ用デプスシェーダー |
| `src/Editor/ViewportPanel.cpp` | エディタの各ビューポートパネルが FBO を持ち `renderViewport` を呼ぶ |
| `include/Core/TerrainStreamer.hpp` | チャンクメッシュ（`Chunk::mesh`）の供給元 |

## 全体フロー

```
main.cpp ループ
  └─ Renderer::render(user, window, workspace)
       ├─ ViewportRenderDesc 構築（Primary Viewport）
       │    editor あり → desc.fbo = editor->getViewportFBO()
       │    editor なし → desc.fbo = 0（デフォルトFB）
       ├─ renderViewport(desc)        ← 3D シーン1回分
       ├─ editor->clearForImGui(window)
       ├─ editor->renderUI(...)       ← ImGui 全パネル
       │    └─ 各 ViewportPanel::onRender()
       │         └─ Renderer::renderViewport(desc)  ← パネルごとに再度3D描画
       │         └─ renderScreenGui / renderWorldGui / renderToolHotbar
       └─ glfwSwapBuffers / glfwPollEvents
```

エディタ使用時は **Primary Viewport 用に1回 + 開いているビューポートパネルごとに1回**、
`renderViewport` がそれぞれ独立した FBO に対して実行される（マルチビューポート対応）。

## `renderViewport(desc)` の内部処理

`src/Core/Renderer.cpp` の `Renderer::renderViewport`。1回の呼び出しで以下を順に行う。

1. **状態保存**: 現在の FBO バインディングとビューポートを退避
2. **対象 FBO へバインド** + `glViewport` + クリア
3. `view`/`projection` 行列を `desc.cameraPosition/Forward/Up` から構築（FOV 45°, near 0.1, far 10000）
4. Workspace ツリーから `Lighting` インスタンスを再帰探索 (`findLightingInTree`)
5. **Skybox 同期**: `desc.isFocused` のときだけ Skybox インスタンスをカメラ位置へ追従
6. **Shadow Pass**（`desc.renderShadows` かつ Lighting/shadowFBO が有効な場合のみ）
   - ライト方向から正射影のライトビュー/プロジェクションを構成し `lightSpaceMatrix` を計算
   - `shadowFBO`（2048×2048 深度テクスチャ）へ `depthShader` でシーン全体（`BaseCube` 系 + Terrain チャンク）を描画
   - 完了後メイン FBO に戻す
7. **Main Pass**
   - `shaderProgram` を使用、`view`/`projection`/`viewPos`/`lightDir`/`brightness` をセット
   - シャドウマップをテクスチャユニット1にバインド
   - Workspace を再帰走査し、`Cube`/`Cylinder`/`TriangularPrism`/`Sphere`（すべて `BaseCube` 派生）を `Color.a > 0` のときだけ `draw()`
   - 各インスタンスの `Unlit`/`UseTriplanar`/`TextureScale` をユニフォームに反映
8. **選択ハイライト**（`desc.renderHighlights` かつエディタの選択中インスタンスがある場合）
   - 102% スケールでワイヤーフレーム（`glPolygonMode(GL_LINE)`）の黄色アウトラインを上書き描画
9. **制約ビジュアライズ**（`desc.renderConstraints`）: `renderConstraints()` で Rope（二次ベジェ近似の垂れ下がり線）/ Rod（直線）を `m_lineShader` で描画
10. **Terrain 描画**: `renderTerrain()` で `TerrainStreamer::getChunks()` の全チャンクメッシュを描画（頂点カラー使用、ライティングはメインシェーダーと共通）
11. 退避していた FBO・ビューポートに復帰。`desc.renderHighlights` が立っている呼び出し（Primary Viewport）の view/proj を `m_lastView`/`m_lastProj` に保存（GUI のワールド→スクリーン投影に使用）

## シェーダー仕様

### メインシェーダー（`vertex.glsl` / `fragment.glsl`）

- 頂点属性: `aPos`(0), `aNormal`(1), `aTexCoord`(2), `aVertexColor`(3, Terrain用)
- ライティング: Lambert 拡散光 + 固定 ambient(0.3) のみ。スペキュラなし
- シャドウ: 3×3 PCF、深度バイアスは法線とライト方向の内積に応じて可変
- `useTriplanar`: ON のとき FragPos の3平面（XY/XZ/ZY）から法線ブレンドでサンプリングしテクスチャの伸び・タイル割れを回避
- `useVertexColor`: ON のとき頂点カラー（Terrain）を直接 baseColor として使用しテクスチャ合成をスキップ
- `unlit`: ON のときライティング計算をスキップして `ourColor`/テクスチャのみで出力（GUI ベイク・デカール等）
- `isSurfaceGui`: SurfaceGui テクスチャ合成時のみ通常と異なるブレンド式（`mix(ourColor, texColor, texColor.a)`、色を乗算しない）

### デプスシェーダー（`depth_vertex.glsl` / `depth_fragment.glsl`）

シャドウパス専用。`lightSpaceMatrix * model * position` を出力するのみ（カラー出力なし、深度のみ）。

## 描画対象別の扱い

| 種別 | 描画経路 |
|---|---|
| `Cube`/`Cylinder`/`TriangularPrism`/`Sphere`（`BaseCube` 派生） | メインパスでツリー再帰、各クラスの `draw(modelLoc, shaderProgram)` |
| `Cube` のフェイス装飾（`Decal`/`Texture`/`SurfaceGui`） | `Cube::draw()` 内でフェイスごとにテクスチャID・UV スケールを切替（`src/Instances/Cube.cpp`） |
| `Terrain` チャンク | `TerrainStreamer::getChunks()` から `Chunk::mesh.VAO` を直接 `glDrawElements`。専用 VAO/VBO で頂点カラー付き |
| `Rope`/`Rod` | `m_lineShader` による `GL_LINE_STRIP`／`GL_LINES`。物理オブジェクトとは別の補助描画パス |
| `ScreenGuiObject`（TextLabel/TextButton 等） | 3D パスとは独立。ImGui の `WindowDrawList` に直接矩形・テキストを描画（`renderScreenGui`） |
| `WorldGuiObject`（BillboardGui/ProximityPrompt 等） | `m_lastView`/`m_lastProj` でワールド座標をスクリーン座標へ射影し、ImGui で描画（`renderWorldGui`） |
| `SurfaceGui` | 専用 FBO にベイクして `Cube` のフェイステクスチャとして 3D 内に合成（`bakeSurfaceGui`） |
| 選択ハイライト・ブラシマーカー・制約線 | いずれもメインシェーダーとは別の単純な `m_lineShader`／ワイヤーフレーム描画として後付け |

## Shadow Map の制約

- 固定サイズ 2048×2048、正射影範囲は ±80（ワールド単位）固定
- シーン全体のうち `BaseCube::CastShadow == true` のインスタンスと Terrain チャンクのみが寄与
- ライト方向は `Lighting.lightDir` のみ参照（複数ライト・ポイントライトのシャドウ未対応）

## マルチビューポートの注意点

- `renderViewport` は呼び出し側が FBO バインド／ビューポート設定済みであることを前提とせず、内部で保存・復元するため、ViewportPanel 側は素朴に呼ぶだけでよい
- ただし `m_lastView`/`m_lastProj` は **Primary Viewport（`desc.renderHighlights == true`）の呼び出しでのみ更新される**ため、サブビューポート上の WorldGui 投影は Primary のカメラ視点を流用する形になっている
- `editor->renderUI` 内でビューポートパネルが複数開かれていれば、その数だけ毎フレーム `renderViewport`（シャドウパス含む）が実行されるため、パネル数に比例して描画コストが増える
