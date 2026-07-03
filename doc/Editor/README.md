# Editor

ImGui ベースのエディタ UI パネル群。`EditorManager` が全パネルを所有し、`Renderer` から呼び出される。

## パネル間のデータフロー

```
EditorManager
  ├─ SceneHierarchyPanel
  │    └─ selectedInstance ──→ (ポインタ共有)
  │                                ├─ PropertiesPanel
  │                                └─ ViewportPanel (ギズモ)
  ├─ ConsolePanel ←── Logger::g_logHook
  ├─ ViewportPanel ←──→ ViewportFocusManager
  ├─ AnimationEditorPanel
  │    └─ selectedInstance ──→ (ポインタ共有、Hierarchyと同じ)
  └─ ContentBrowserPanel

SecondaryViewportPanel（EditorManager非所属・独立ウィンドウ）
  └─ Workspace(weak_ptr) ──→ Renderer::renderViewport()
```

## クラス一覧

| クラス | ファイル | 概要 |
|---|---|---|
| [EditorPanel](EditorPanel.md) | `include/Editor/EditorPanel.hpp` | 抽象基底パネル |
| [EditorManager](EditorManager.md) | `include/Editor/EditorManager.hpp` | 全パネルの統括マネージャ |
| [CommandHistory](CommandHistory.md) | `include/Editor/CommandHistory.hpp` | Undo/Redo スタックと Command サブクラス群 |
| [ConsolePanel](ConsolePanel.md) | `include/Editor/ConsolePanel.hpp` | C++ / Luau ログコンソール |
| [SceneHierarchyPanel](SceneHierarchyPanel.md) | `include/Editor/SceneHierarchyPanel.hpp` | シーン階層ツリービュー |
| [PropertiesPanel](PropertiesPanel.md) | `include/Editor/PropertiesPanel.hpp` | プロパティインスペクタ |
| [ViewportPanel](ViewportPanel.md) | `include/Editor/ViewportPanel.hpp` | 3D ビューポート + ギズモ |
| [ContentBrowserPanel](ContentBrowserPanel.md) | `include/Editor/ContentBrowserPanel.hpp` | アセットファイルブラウザ |
| [ViewportFocusManager](ViewportFocusManager.md) | `include/Editor/ViewportFocusManager.hpp` | ビューポートフォーカス排他制御 |
| [AnimationEditorPanel](AnimationEditorPanel.md) | `include/Editor/AnimationEditorPanel.hpp` | Model の Animation キーフレーム編集パネル |
| [SecondaryViewportPanel](SecondaryViewportPanel.md) | `include/Editor/SecondaryViewportPanel.hpp` | 別Workspaceを表示するフローティングビューポート |
