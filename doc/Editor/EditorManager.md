# EditorManager

`include/Editor/EditorManager.hpp`

全エディタパネルを所有・管理し、ImGui ドックスペースの描画を統括するクラス。

## 列挙型

```cpp
enum class EditorMode { Edit, Play, Pause }
```

## メンバ変数（public）

| 変数 | 型 | 説明 |
|---|---|---|
| `mode` | `EditorMode` | 現在の動作モード |
| `consolePanel` | `unique_ptr<ConsolePanel>` | ログコンソール |
| `hierarchyPanel` | `unique_ptr<SceneHierarchyPanel>` | シーン階層ビュー |
| `propertiesPanel` | `unique_ptr<PropertiesPanel>` | プロパティインスペクタ |
| `contentBrowserPanel` | `unique_ptr<ContentBrowserPanel>` | アセットブラウザ |
| `viewportPanel` | `unique_ptr<ViewportPanel>` | 3D ビューポート |
| `scenePath` | `string` | 保存/読み込み対象のシーンファイルパス（デフォルト: `assets/scenes/test_scene.yaml`） |
| `m_history` | `CommandHistory` | Undo/Redo スタック（各パネルに生ポインタで共有） |

## メソッド（public）

| メソッド | 説明 |
|---|---|
| `render(GLFWwindow*)` | ドックスペース・全パネル `onRender()`・ショートカット処理を行う（メインループから呼ぶ） |
| `render()` | `render(nullptr)` の互換オーバーロード |
| `setWorkspace(ws)` | Stop 後のリロード時に全パネルの workspace ポインタを一括更新し、history をクリアする |
| `beginViewportRender()` | ViewportPanel の FBO にバインド（3D 描画前に呼ぶ） |
| `endViewportRender()` | FBO をアンバインドしてテクスチャを ImGui に転送 |
| `isEditMode()` / `isPlayMode()` / `isPauseMode()` | モード確認 |
| `isDirty()` | 未保存変更があるか |
| `markDirty()` | 未保存フラグを立てる |
| `requestSaveDialog(window)` | 未保存確認ダイアログを次フレームで表示するようリクエスト |

## プライベートメソッド

| メソッド | 説明 |
|---|---|
| `renderToolbar()` | Play/Pause/Stop・ギズモモード・オブジェクト追加・Save/Load ボタンを描画 |
| `handleEditorShortcuts()` | Edit モード中のキーボードショートカット処理 |
| `renderSaveDialog()` | 未保存確認モーダルダイアログの ImGui 描画 |
| `saveCurrentScene()` | `SceneLoader::saveScene()` でシーンを保存し `m_isDirty` をクリア |
| `applyTheme()` | 起動時に呼ぶ ImGui カラーテーマ適用 |

## キーボードショートカット（Edit モードのみ）

| ショートカット | 動作 |
|---|---|
| `Ctrl+S` | シーン保存（テキスト入力中でも有効） |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Backspace` | 選択インスタンスを削除（Viewport 非フォーカス時） |
| `Ctrl+C` | 選択インスタンスをクリップボードへコピー |
| `Ctrl+V` | クリップボードから兄弟として貼り付け |
| `Ctrl+Shift+V` | クリップボードから選択インスタンスの子として貼り付け |

## ツールバーボタン

Play/Pause/Stop、Select/Move/Resize/Rotate（ギズモモード）、New Cube/New Cylinder/New Prism/New Sphere、Save/Load。  
オブジェクト追加は `CommandHistory` 経由で Undo 対応。名前衝突は連番サフィックスで自動回避。

## パネル間のデータ共有

```
hierarchyPanel->selectedInstance
        ↓（ポインタを共有）
propertiesPanel->selectedInstance  ← 同じ Instance* を参照
viewportPanel->selectedInstance    ← 同じ Instance* を参照してギズモを描画

m_history（CommandHistory）
        ↓（生ポインタで共有）
hierarchyPanel->m_history
propertiesPanel->m_history
viewportPanel->m_history

m_clipboard（shared_ptr<Instance>）
        ↓（生ポインタで共有）
hierarchyPanel->m_clipboard
```

## 依存関係

- ImGui, ImGuizmo
- `ConsolePanel`, `SceneHierarchyPanel`, `PropertiesPanel`, `ContentBrowserPanel`, `ViewportPanel`
- `CommandHistory`, `ViewportFocusManager`
- `Workspace`, `User`, `SceneLoader`

## 使われる場所

- `Renderer` が `unique_ptr<EditorManager>` として所有
- `Renderer::renderImGui()` から `render()` を呼ぶ
