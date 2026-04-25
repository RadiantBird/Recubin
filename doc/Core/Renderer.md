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
| `textureCache` | `map<string, GLuint>` | パス → テクスチャ ID キャッシュ |
| `whiteTexture` | `GLuint` | デフォルトフォールバックテクスチャ |
| `editor` | `unique_ptr<EditorManager>` | エディタ UI マネージャ |

## メソッド

| メソッド | 説明 |
|---|---|
| `init(window)` | OpenGL 状態・ImGui・シェーダーを初期化 |
| `render(user, window, workspace)` | メインレンダーエントリ（3D + ImGui を 1 フレーム描画） |
| `renderScene(user, workspace)` | 3D シーンのみを描画（FBO へ書き込む） |
| `renderImGui(user, window, workspace)` | ImGui パネルを描画 |
| `loadTexture(path)` | 画像ファイルを GPU にアップロードして ID を返す |
| `createWhiteTexture()` | 1×1 白テクスチャを生成 |
| `loadShaderSource(filePath)` | GLSL ソースをファイルから読み込む |

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

## 依存関係

- GLEW, GLFW, ImGui
- `EditorManager`, `User`, `Workspace`, `FileLoader`, `Matrix4`

## 使われる場所

- `main.cpp` のメインループで毎フレーム `render()` を呼ぶ
