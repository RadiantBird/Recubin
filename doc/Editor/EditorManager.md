# EditorManager

`include/Editor/EditorManager.hpp`

全エディタパネルを所有・管理し、ImGui ドックスペースの描画を統括するクラス。

## 列挙型

```cpp
enum EditorMode { Edit, Play, Pause }
```

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `mode` | `EditorMode` | 現在の動作モード |
| `consolePanel` | `unique_ptr<ConsolePanel>` | ログコンソール |
| `hierarchyPanel` | `unique_ptr<SceneHierarchyPanel>` | シーン階層ビュー |
| `propertiesPanel` | `unique_ptr<PropertiesPanel>` | プロパティインスペクタ |
| `contentBrowserPanel` | `unique_ptr<ContentBrowserPanel>` | アセットブラウザ |
| `viewportPanel` | `unique_ptr<ViewportPanel>` | 3D ビューポート |

## メソッド

| メソッド | 説明 |
|---|---|
| `render()` | ドックスペース + 全パネルの `onRender()` を呼ぶ |
| `beginViewportRender()` | ViewportPanel の FBO にバインド（3D 描画前に呼ぶ） |
| `endViewportRender()` | FBO をアンバインドしてテクスチャを ImGui に転送 |
| `isEditMode()` / `isPlayMode()` / `isPauseMode()` | モード確認 |
| `renderToolbar()` *(private)* | Play / Pause / Stop ボタンを描画 |
| `applyTheme()` *(private)* | ImGui カラーテーマを適用 |

## パネル間のデータ共有

```
hierarchyPanel->selectedInstance
        ↓（ポインタを共有）
propertiesPanel->selectedInstance  ← 同じ Instance* を参照
viewportPanel->selectedInstance    ← 同じ Instance* を参照してギズモを描画
```

## 依存関係

- ImGui
- `ConsolePanel`, `SceneHierarchyPanel`, `PropertiesPanel`, `ContentBrowserPanel`, `ViewportPanel`
- `Workspace`, `User`

## 使われる場所

- `Renderer` が `unique_ptr<EditorManager>` として所有
- `Renderer::renderImGui()` から `render()` を呼ぶ
