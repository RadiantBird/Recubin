# ViewportPanel

`include/Editor/ViewportPanel.hpp`

3D シーンを FBO でレンダリングして ImGui 内に表示し、ImGuizmo によるギズモ操作でオブジェクトを編集するパネル。

## 継承

`EditorPanel` → `ViewportPanel`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `framebuffer` | `GLuint` | フレームバッファオブジェクト（FBO） |
| `colorTexture` | `GLuint` | カラーアタッチメントテクスチャ |
| `depthRenderbuffer` | `GLuint` | 深度バッファ |
| `fbWidth`, `fbHeight` | `int` | FBO の解像度（デフォルト 1280×720） |
| `gizmoOp` | `ImGuizmo::OPERATION` | 現在のギズモモード（TRANSLATE / SCALE / ROTATE） |
| `selectOnly` | `bool` | true のとき ImGuizmo を描画しない（Select モード） |
| `selectedInstance` | `Instance**` | 選択中インスタンスへのポインタ（`SceneHierarchyPanel` と共有） |
| `user` | `User*` | カメラ行列・View 行列を取得するためのポインタ |
| `workspace` | `Workspace*` | ピッキング判定用 |
| `isViewportFocused` | `bool` | このビューポートが現在フォーカスされているか |
| `isHoveringViewport` | `bool` | マウスがビューポート上にあるか |
| `m_isDraggingSelected` | `bool` | 選択オブジェクト上でドラッグ開始したか（ドラッグ判定用） |
| `m_history` | `CommandHistory*` | Undo/Redo スタック（EditorManager から借用） |

## メソッド

| メソッド | 説明 |
|---|---|
| `initFBO(w, h)` | FBO・テクスチャ・深度バッファを生成 |
| `resizeFBO()` | ウィンドウリサイズ時に FBO を再生成 |
| `destroyFBO()` | FBO を解放 |
| `beginRender()` | FBO にバインドして 3D 描画を受け付ける状態にする |
| `endRenderAndDisplay()` | FBO をアンバインドし、テクスチャを ImGui Image で表示 |
| `onRender()` | ギズモ操作・フォーカス更新を含む ImGui ウィンドウを描画 |

## 描画フロー

```
Renderer::render()
  ├─ viewportPanel->beginRender()   ← FBO バインド
  ├─ Cube::draw() × n               ← 3D 描画
  ├─ viewportPanel->endRenderAndDisplay()
  └─ viewportPanel->onRender()
         ├─ ImGui::Image(colorTexture)
         ├─ ViewportFocusManager::SetViewportFocus(this)
         └─ ImGuizmo::Manipulate()  ← ギズモ変換 → setProperty() で反映
```

## 依存関係

- `EditorPanel`, ImGui, ImGuizmo
- OpenGL（`GLuint`）
- `User`, `Instance`
- `ViewportFocusManager`
