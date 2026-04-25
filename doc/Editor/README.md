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
  └─ ContentBrowserPanel
```

## クラス一覧

| クラス | ファイル | 概要 |
|---|---|---|
| [EditorPanel](EditorPanel.md) | `include/Editor/EditorPanel.hpp` | 抽象基底パネル |
| [EditorManager](EditorManager.md) | `include/Editor/EditorManager.hpp` | 全パネルの統括マネージャ |
| [ConsolePanel](ConsolePanel.md) | `include/Editor/ConsolePanel.hpp` | C++ / Luau ログコンソール |
| [SceneHierarchyPanel](SceneHierarchyPanel.md) | `include/Editor/SceneHierarchyPanel.hpp` | シーン階層ツリービュー |
| [PropertiesPanel](PropertiesPanel.md) | `include/Editor/PropertiesPanel.hpp` | プロパティインスペクタ |
| [ViewportPanel](ViewportPanel.md) | `include/Editor/ViewportPanel.hpp` | 3D ビューポート + ギズモ |
| [ContentBrowserPanel](ContentBrowserPanel.md) | `include/Editor/ContentBrowserPanel.hpp` | アセットファイルブラウザ |
| [ViewportFocusManager](ViewportFocusManager.md) | `include/Editor/ViewportFocusManager.hpp` | ビューポートフォーカス排他制御 |
