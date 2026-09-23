# SceneHierarchyPanel

`include/Editor/SceneHierarchyPanel.hpp`

シーンの Instance ツリーを ImGui でツリービュー表示し、オブジェクト選択を管理するパネル。

## 継承

`EditorPanel` → `SceneHierarchyPanel`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `workspace` | `Workspace*` | 表示対象のワークスペース |
| `selectedInstance` | `Instance*` | 現在選択中のインスタンス（`PropertiesPanel` / `ViewportPanel` と共有） |
| `selectedInstances` | `vector<Instance*>` | 複数選択を安定した順序で保持する |
| `m_history` | `CommandHistory*` | Undo/Redo スタック（EditorManager から借用） |
| `m_clipboard` | `shared_ptr<Instance>*` | コピー&ペースト用クリップボード（EditorManager::m_clipboard へのポインタ） |

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | ツリービュー ImGui ウィンドウを描画 |
| `drawNode(inst, registerVisible, renderChildren)` *(private)* | ノードを再帰的に描画（右クリックコンテキストメニュー、可視行クリッピング含む） |
| `renderNewScriptDialog()` *(private)* | Script インスタンス追加ダイアログを描画（ポップアップ外ファイルピッカーと連携） |

## Explorer の並び順

各親の直下の子は、次の優先順位で表示する。

`Workspace` → `Folder` → `Model` → `Script` → `LocalScript` → `ModuleScript` →
物理ファイル (`PhysicalFileInstance`) → `ValueBase` → `BaseCube` → その他

同じ優先グループ内では具体的なクラスをまとめ、同じクラスの名前を ASCII 順で比較する。
名前中の連続した数字は数値として比較するため、`Cube1`、`Cube2`、`Cube10` の順になる。
優先順位がないクラスは最後尾で、クラス名を決定キーにする。並び替えは表示用の一時的な
ポインタ配列に対して行うため、Instance の所有関係や名前キーは変更しない。

各親の直接子は `DirectChildrenCache` に保持する。通常の描画では前回のソート済みポインタ配列を
再利用し、子の追加・削除・親変更・名前変更で親の `children revision` が変化した場合だけ、その親の
配列を再構築してソートする。Workspace切替時はExplorer側のキャッシュ全体を破棄する。ImGui の
ツリーノード ID、選択状態、展開状態は Instance ポインタを基準にしているため、並び替えで失われない。

展開された親に64個以上の閉じた兄弟行が連続する場合、`ImGuiListClipper`で現在のスクロール範囲に見える行だけImGui itemを生成する。子を持つCube等も閉じていれば対象となり、展開中のノードだけ通常の再帰描画へ分離する。Shift範囲選択用の論理的な表示順には画面外の行も保持するため、クリッピングによって選択範囲は変わらない。Ctrl+FのReveal対象とF2のrename対象はclip範囲外でも強制的にitemを生成する。

## 動作

- ツリー上でクリックすると `selectedInstance` が更新され、`PropertiesPanel` / `ViewportPanel` にリアルタイム反映される
- 通常クリックと Ctrl クリックは次回 Shift 選択のアンカーを更新する。Shift クリックは、そのフレームで展開されている可視行の深さ優先順にアンカーからクリック先までを両端込みで選択する。Ctrl+Shift では既存選択へ重複なく追加する
- アンカーが削除済み、別 Workspace、または折りたたみ内で非表示の場合、Shift クリックはクリック先だけへ安全にフォールバックする
- 右クリックの「子をすべて選択」は対象自身や孫を含めず、直下の子だけを Explorer 表示順で選択する。選択変更は Undo/Redo やシーンの dirty 状態へ影響しない
- インスタンスの追加・削除・親変更は `m_history` 経由で Undo 対応
- Script 追加時はダイアログで新規ファイル作成 or 既存ファイル選択を選べる（`m_doPick` / `m_pickExisting` フラグでポップアップ外ファイルピッカーを遅延実行）
- `Script` / `TextFile` の行をダブルクリックすると `onOpenEditor` コールバックへ同じ Instance の `shared_ptr` を渡し、中央 Dock の `CodeEditorPanel` を開く。通常のクリック選択や複数選択の挙動は変えない。
- 右クリックの「グループ化」は選択項目を Model / Folder / Tool または Insert Object の各種コンテナへまとめる。生成と親変更は `GroupInstancesCommand` の1 Undo単位で処理し、Spatial 子孫のワールド姿勢を保持する。

## 依存関係

- `EditorPanel`, `Workspace`, `Instance`, `CommandHistory`, ImGui
