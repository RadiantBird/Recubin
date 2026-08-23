# Renderer

`include/Core/Renderer.hpp`

OpenGL レンダリングパイプライン全体を管理するシングルトン。3D シーン描画と ImGui エディタ UI を統括する。

## 設計

シングルトン（`static Renderer* instance`）

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `VBO`, `VAO`, `EBO` | `GLuint` | 頂点バッファオブジェクト |
| `shaderProgram` | `GLuint` | コンパイル済み GLSL シェーダー |
| `indices` | `vector<GLuint>` | インデックスバッファ |
| `textureCache` | `map<string, TextureCacheEntry>` | 正規化パス → テクスチャ IDとファイルメタデータのキャッシュ |
| `whiteTexture` | `GLuint` | デフォルトフォールバックテクスチャ |
| `editor` | `unique_ptr<EditorManager>` | エディタ UI マネージャ |

## メソッド

| メソッド | 説明 |
|---|---|
| `init(window)` | OpenGL 状態・ImGui・シェーダーを初期化 |
| `render(user, window, workspace)` | メインレンダーエントリ（3D + ImGui を 1 フレーム描画） |
| `renderScene(user, workspace)` | 3D シーンのみを描画（FBO へ書き込む） |
| `renderImGui(user, window, workspace)` | ImGui パネルを描画 |
| `loadTexture(path)` | 画像ファイルを GPU にアップロードして ID を返す。同一正規化パスのキャッシュは、ロード時の最終更新時刻またはファイルサイズが変わった場合に破棄して再読み込みする。ファイル情報を取得できない場合は既存の正常なキャッシュを維持する |
| `loadTextureFromMemory(data, size)` | メモリ上の画像データから GPU テクスチャを生成 |
| `createWhiteTexture()` | 1×1 白テクスチャを生成 |
| `loadShaderSource(filePath)` | GLSL ソースをファイルから読み込む |
| `renderViewport(desc)` | `ViewportRenderDesc` を受け取る統合ビューポート描画（シャドウ/ハイライト/コンストレイント/ポストエフェクトの各フラグを見て描画） |
| `initLineRenderer()` / `renderConstraints(ws, view, proj)` | Rope/Rod 制約の可視化用ラインレンダラーの初期化・描画 |
| `renderBrushMarker(view, proj, center, radius)` | 地形ブラシのヒット位置を示す水平リングを描画（呼び出し側が FBO バインド済みであること） |

`ViewportRenderDesc::renderRenderingDebug` は物理デバッグとは独立した描画デバッグの切替である。
SurfaceMark の投影ボリューム表示など、レンダリング由来の可視化はこのフラグだけで有効化する。

SurfaceMark オーバーレイは既存の `shaderProgram` に組み込まれており、`uSurfaceMarkPass` が有効な
候補再描画時だけ world position を投影ローカルへ変換する。画像は texture unit 2、マーク視点の
最前面深度は texture unit 3、既存のシャドウマップは unit 1 を使用する。深度生成は既存の
`depthShader` を再利用し、`uTime` / `uIsLiquid` により LiquidCube の波形をメインパスと一致させる。
| `initPostEffectRenderer()` / `ensurePostEffectFBOs(w, h)` / `renderPostEffects(ws, targetFbo, w, h)` | `PostEffect` インスタンスを ZIndex 順に適用するポストエフェクトチェーン |
| `renderTerrain(view, proj, workspace)`（private） | `Workspace` 内の `Terrain`（`TerrainStreamer::getChunks()`）を描画 |

## レンダーフロー

```
render()
  ├─ editor->beginViewportRender()   ← FBO にバインド
  ├─ renderScene()
  │    └─ Workspace 内の全 Cube::draw() を呼ぶ
  │    └─ User のカメラ行列を u_view / u_projection に転送
  ├─ editor->endViewportRender()     ← FBO をアンバインド
  └─ renderImGui()
       └─ editor->render()           ← 全パネルを描画
```

## GUI 描画（src/Core/Renderer_GUI.cpp）

`Renderer` クラス自体の実装で、`src/Core/Renderer_GUI.cpp` に分割されている（別クラスではない）。ScreenGui/WorldGui/ツールホットバー/ゲーム内GUI・SurfaceGui のベイクを担当する。

| メソッド | 説明 |
|---|---|
| `renderScreenGui(ws, vpX, vpY, vpW, vpH)` | `ScreenGuiObject` 系（画面固定UI）をビューポート座標系で描画。`Norm::Pixel` 要素は `ws` の親 `System.BaseResolution` と実際のビューポート解像度(vpW/vpH)の比率(X,Y別々)でスケーリングする（`System` が見つからない場合は等倍） |
| `renderWorldGui(ws, vpX, vpY, vpW, vpH)` | `WorldGuiObject`（3D空間内に配置されるGUI、BillboardGui等）を描画 |
| `renderToolHotbar(user, vpX, vpY, vpW, vpH)` | `User` が所持するツールのホットバーUIを描画 |
| `renderGameGui(ws, user, vpX, vpY, vpW, vpH)` | Play モード時のゲーム内 GUI をまとめて描画 |
| `bakeSurfaceGui(sg)` | `SurfaceGui` の内容をテクスチャへベイクし、対象キューブ面に貼り付けられるようにする |

## 依存関係

SurfaceMarkの投影では各マークのFilterMode/FilterInstancesをvolume cullより先に評価する。拒否対象は深度生成とoverlayの両方から除外されるため、除外物を貫通して奥の許可対象へ投影できる。

- GLEW, GLFW, ImGui, PhysX（コンストレイント可視化）
- `EditorManager`, `User`, `Workspace`, `FileLoader`, `Matrix4`
- `Terrain`, `TerrainStreamer`（地形描画）
- `GuiButton`, `SurfaceGui`（GUI 描画）

## 使われる場所

- `main.cpp` のメインループで毎フレーム `render()` を呼ぶ
