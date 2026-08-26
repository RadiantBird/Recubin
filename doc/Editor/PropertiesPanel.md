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

複数選択時は Name の入力で選択順に base/base1/base2... を割り当て、Spatial の
Position/Size/CFrame を一括編集できる。Position と CFrame はワールド座標で適用し、
展開した XYZ 編集では変更軸だけを反映する。全変更は一つの複合履歴として記録される。

## 依存関係

SurfaceMarkにはFilterModeコンボとFilterInstancesリストを表示する。任意Instance PickerによるAdd、行ごとのRemove、Clearを専用Undoコマンドで操作でき、未解決パスも編集対象として表示する。

- `EditorPanel`, `Instance`, `CommandHistory`, ImGui
