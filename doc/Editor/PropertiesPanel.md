# PropertiesPanel

`include/Editor/PropertiesPanel.hpp`

選択中の Instance のプロパティを表示・編集するインスペクタパネル。

## 継承

`EditorPanel` → `PropertiesPanel`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `selectedInstance` | `Instance**` | `SceneHierarchyPanel::selectedInstance` へのポインタ |
| `m_history` | `CommandHistory*` | Undo/Redo スタック（EditorManager から借用） |

`Instance**` を持つことで、`SceneHierarchyPanel` で選択が変わるたびに自動的に最新の選択を反映する。

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | `*selectedInstance` のプロパティを編集できる ImGui ウィンドウを描画 |

## 編集可能なプロパティ例

| プロパティ | 対象クラス |
|---|---|
| `Name` | Instance |
| `Position`, `Rotation`, `Size` | Spatial |
| `Color` | BaseCube |
| `Anchored`, `CanCollide` | BaseCube |

値を変更すると `m_history` 経由で対応する Command（`SetVec3Command`、`SetColorCommand`、`SetBoolCommand` など）を記録し、Undo 対応で反映する。

## 依存関係

- `EditorPanel`, `Instance`, `CommandHistory`, ImGui
