# SecondaryViewportPanel

`include/Editor/SecondaryViewportPanel.hpp`

指定 Workspace を独立したフリーカメラで表示するフローティング ImGui ウィンドウ。メインの `ViewportPanel` とは別に、任意の Workspace（例: プレビュー用の別シーン）を同時に確認するために使う。`EditorPanel` は継承せず単独クラスとして実装されている。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_open` | `bool` | ウィンドウの開閉状態 |
| `m_workspace` | `weak_ptr<Workspace>` | 表示対象の Workspace（弱参照） |
| `m_title` | `string` | ウィンドウタイトル |
| `m_fbo` | `GLuint` | フレームバッファオブジェクト |
| `m_colorTex` | `GLuint` | カラーアタッチメントテクスチャ |
| `m_depthRbo` | `GLuint` | 深度/ステンシルレンダーバッファ |
| `m_fbWidth`, `m_fbHeight` | `int` | FBO 解像度（デフォルト 640×360、ウィンドウサイズに追従） |
| `m_camPos` | `Vector3` | フリーカメラ位置 |
| `m_yaw`, `m_pitch` | `float` | カメラ向き（度） |
| `m_speed` | `float` | カメラ移動速度 |

## メソッド

| メソッド | 説明 |
|---|---|
| `SecondaryViewportPanel(ws, title)` | コンストラクタ。FBO を初期化 |
| `~SecondaryViewportPanel()` | FBO を解放 |
| `onRender()` | ImGui ウィンドウ描画・カメラ入力処理・`Renderer::renderViewport()` 呼び出し・FBO テクスチャ表示 |
| `initFBO(w, h)` / `resizeFBO(w, h)` / `destroyFBO()` | FBO 生成・リサイズ・破棄（private） |
| `getCamRot()` | yaw/pitch から向きの `Quaternion` を算出（private） |

## フロー

```
onRender()
  ImGui::Begin(m_title)
  ウィンドウサイズ変化 → resizeFBO()
  右クリックドラッグ → yaw/pitch 更新
  フォーカス中 → WASD+QE でカメラ移動（getCamRot() の forward/right 基準）
  Workspace が生きていれば
    ViewportRenderDesc 組み立て → Renderer::instance->renderViewport(desc)
  ImGui::Image(m_colorTex) で表示
```

## 依存関係

- `Core/Renderer`（`ViewportRenderDesc`, `Renderer::instance->renderViewport()`）
- `Instances/Workspace`
- `Math/Vector3`, `Math/Quaternion`
- OpenGL（`GLuint`）, ImGui

## 使われる場所

- `EditorManager` とは独立して、別 Workspace のプレビュー用途で個別に生成・所有される想定
