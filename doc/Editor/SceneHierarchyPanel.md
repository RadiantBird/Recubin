# SceneHierarchyPanel

`include/Editor/SceneHierarchyPanel.hpp`

シーンの Instance ツリーを ImGui でツリービュー表示し、オブジェクト選択を管理するパネル。

## 継承

`EditorPanel` → `SceneHierarchyPanel`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `workspace` | `Workspace*` | 表示対象のワークスペース |
| `selectedInstance` | `Instance*` | 現在選択中のインスタンス |

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | ツリービュー ImGui ウィンドウを描画 |
| `drawNode(inst)` *(private)* | ノードを再帰的に描画 |

## 動作

- ツリー上でクリックすると `selectedInstance` が更新される
- `selectedInstance` は `EditorManager` を通じて `PropertiesPanel` と `ViewportPanel` に共有ポインタとして渡されるため、クリックするとリアルタイムで他パネルも更新される

## 依存関係

- `EditorPanel`, `Workspace`, `Instance`, ImGui
