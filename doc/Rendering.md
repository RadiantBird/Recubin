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
| `shaders/vertex.glsl`, `shaders/fragment.glsl` | メインシェーダー（Phong + シャドウ + トライプラナー） |
| `shaders/depth_vertex.glsl`, `shaders/depth_fragment.glsl` | シャドウマップ用デプスシェーダー |
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
   - BaseCube は102% スケールでワイヤーフレーム（`glPolygonMode(GL_LINE)`）の黄色アウトラインを上書き描画
   - 直接親が `Cube` の Decal は、親Cube全体ではなく対象Faceの外縁4辺を固定ピクセル幅リボンで描画する
9. **制約ビジュアライズ**（`desc.renderConstraints`）: `renderConstraints()` で Rope（二次ベジェ近似の垂れ下がり線）/ Rod（直線）を `m_lineShader` で描画
10. **Terrain 描画**: `renderTerrain()` で `TerrainStreamer::getChunks()` の全チャンクメッシュを描画（頂点カラー使用、ライティングはメインシェーダーと共通）
11. **雲パス**: `renderClouds()` で Workspace 直下から `Weather` を探し（`Enabled` かつ見つかった場合のみ）、カメラ直上に毎フレーム再配置する巨大水平クアッド（`aPos`(vec3)+`aUV`(vec2)）に、起動時に一度だけ `PerlinNoise::fbm2` で焼いた 512×512 グレースケールテクスチャ（`m_cloudNoiseTex`、実行時の再焼き込みはしない）を `Weather::WindDirection` 由来の UV オフセットでスクロールサンプルし、`CloudCover`/`CloudDensity` でしきい値・不透明度を計算する専用シェーダー `m_cloudShader` で描画する。`glDepthMask(GL_FALSE)` で自己遮蔽を避けつつ深度テストは有効のまま（地形などには正しく隠れる）
12. **雷柱パス**: `renderLightning()` で Workspace 直下の `Weather` から `getLightningBolt()`（落雷時に `Weather::attemptStrike()` が中点変位法で生成したジグザグ頂点列）と `getLightningBoltAlpha()`（フラッシュ減衰と同期する線形アルファ）を読み、頂点列が2点未満またはアルファ0以下なら即 return。それ以外は雲パスとは別に、Rope/Rod/地形ブラシガイドと同じ `m_lineShader`／`m_lineVAO` を再利用して `GL_LINE_STRIP`（`glLineWidth(3.0f)`）で描画する（新規 GL リソースなし）
13. **パーティクル描画**: `renderParticles()` で `ParticleEmitter` をツリーから収集し、粒子ごとにカメラ基底(`cameraRight`/`cameraUp`)でビルボード展開した頂点（`aPos`(vec3)+`aColor`(vec4)、テクスチャなし単色頂点シェーダー `m_particleShader`）を1つの頂点バッファへまとめ、`glDepthMask(GL_FALSE)`で自己遮蔽を避けつつ1回の`glDrawArrays(GL_TRIANGLES)`で描画する。粒子の状態更新（位置・寿命・発生）はここでは行わず、メインループから毎フレーム1回だけ呼ばれる`ParticleEmitter::updateAll()`が担う（`renderViewport`はビューポートの数だけ複数回呼ばれるため、状態更新をここに置くと多重更新になる）。`Weather`の雨/雪も内部で`ParticleEmitter`を使っているため、この既存パスがそのまま描画する
14. 退避していた FBO・ビューポートに復帰。`desc.renderHighlights` が立っている呼び出し（Primary Viewport）の view/proj を `m_lastView`/`m_lastProj` に保存（GUI のワールド→スクリーン投影に使用）

## シェーダー仕様

### メインシェーダー（`vertex.glsl` / `fragment.glsl`）

- 頂点属性: `aPos`(0), `aNormal`(1), `aTexCoord`(2), `aVertexColor`(3, Terrain用)
- ライティング: Lambert 拡散光 + 固定 ambient(0.3) のみ。スペキュラなし
- シャドウ: 3×3 PCF、深度バイアスは法線とライト方向の内積に応じて可変（最小 0.0005、slope-scale 最大 0.0015）。バイアスを小さくして接地影を復元する一方、自己シャドウのアクネが発生しやすくなるトレードオフがある
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
| `ParticleEmitter` | `m_particleShader` によるビルボード`GL_TRIANGLES`。全発生源の全粒子を1つの頂点バッファに集約し1ドローコールで描画 |
| `Weather`（雲層） | `m_cloudShader` によるカメラ追従の巨大水平クアッド。ノイズは起動時に一度だけ焼いたテクスチャをスクロールサンプル |
| `Weather`（雷柱） | `m_lineShader` による `GL_LINE_STRIP`。頂点列は落雷時に `Weather::attemptStrike()` が中点変位法で生成し、フラッシュ減衰と同期したアルファでフェード |
| `ScreenGuiObject`（TextLabel/TextButton 等） | 3D パスとは独立。ImGui の `WindowDrawList` に直接矩形・テキストを描画（`renderScreenGui`） |
| `WorldGuiObject`（BillboardGui/ProximityPrompt 等） | `m_lastView`/`m_lastProj` でワールド座標をスクリーン座標へ射影し、ImGui で描画（`renderWorldGui`） |
| `SurfaceGui` | 専用 FBO にベイクして `Cube` のフェイステクスチャとして 3D 内に合成（`bakeSurfaceGui`） |
| 選択ハイライト・ブラシマーカー・制約線 | いずれもメインシェーダーとは別の単純な `m_lineShader`／ワイヤーフレーム描画として後付け |

## Shadow Map の制約

- 固定サイズ 2048×2048、正射影範囲は ±80（ワールド単位）固定
- `CastShadow == false` は常に影なし。true の場合は `ShadowMode`（Always/Never/Normal）で判定し、Normal は `Color.a > 0.001`、MeshCube の fallback geometry は例外として影を生成する。深度テクスチャは `GL_LINEAR`、シェーダは既存の3×3 PCFを使用する。深度バイアスは最小 0.0005、slope-scale 最大 0.0015 で、接地影と自己シャドウのアクネをバランスする
- ライト方向は `Lighting.lightDir` のみ参照（複数ライト・ポイントライトのシャドウ未対応）

## マルチビューポートの注意点

- `renderViewport` は呼び出し側が FBO バインド／ビューポート設定済みであることを前提とせず、内部で保存・復元するため、ViewportPanel 側は素朴に呼ぶだけでよい
- ただし `m_lastView`/`m_lastProj` は **Primary Viewport（`desc.renderHighlights == true`）の呼び出しでのみ更新される**ため、サブビューポート上の WorldGui 投影は Primary のカメラ視点を流用する形になっている
- `editor->renderUI` 内でビューポートパネルが複数開かれていれば、その数だけ毎フレーム `renderViewport`（シャドウパス含む）が実行されるため、パネル数に比例して描画コストが増える
