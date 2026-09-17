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

BaseCubeのプロパティは、`Appearance` → `Physics` → `Material` → `Editor` のカテゴリ順で表示する。
各カテゴリの見出しはカテゴリ先頭に一度だけ表示し、同じ見出しを重複させない。

値を変更すると `m_history` 経由で対応する Command（`SetVec3Command`、`SetColorCommand`、`SetBoolCommand` など）を記録し、Undo 対応で反映する。

`Vector3` は `x, y, z`、`CFrame` は
`x, y, z, pitch, yaw, roll` のカンマ区切りテキスト入力でも編集できる。CFrame
の後半3値は度数法のXYZ内因Euler角である。テキスト入力はEnter時にだけ確定し、
未指定の末尾成分は最後に指定した値で補完する（例: `1,` は`1, 1, 1`）。
成分数超過、非数、無限値は値を変更せず入力エラーとして表示する。

`Color4` はRGBAそれぞれの0〜255入力欄、`R, G, B, A` の一括入力欄、および
`Palette` ボタンから開くImGuiカラーピッカーを提供する。エンジン内部とシーンの
Color4値は従来どおり0〜1のfloatで保持する。

`Vector3`、`CFrame`、`Color4`には`丸`ボタンを表示し、各成分を四捨五入して
整数へ丸める。CFrameは位置と度数法Euler角を丸めてからQuaternionへ再変換する。
数値列入力と操作ボタンは別行に配置し、狭いPropertiesPanelでもボタンが画面外へ
押し出されないようにする。

複数選択時は Name の入力で選択順に base/base1/base2... を割り当て、Spatial の
Position/Size/CFrame を一括編集できる。Position と CFrame はワールド座標で適用し、
展開した XYZ 編集では変更軸だけを反映する。スキーマプロパティは和集合で表示し、
対応する型だけへ一括適用する（表示名の`(n/m)`は適用対象数）。全変更は一つの複合履歴として記録される。
数値列入力・Color4の成分入力・Paletteも同じ一括適用と複合履歴の対象である。

## 依存関係

SurfaceMarkにはFilterModeコンボとFilterInstancesリストを表示する。任意Instance PickerによるAdd、行ごとのRemove、Clearを専用Undoコマンドで操作でき、未解決パスも編集対象として表示する。

- `EditorPanel`, `Instance`, `CommandHistory`, ImGui
