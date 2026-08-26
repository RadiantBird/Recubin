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
| `drawNode(inst)` *(private)* | ノードを再帰的に描画（右クリックコンテキストメニュー含む） |
| `renderNewScriptDialog()` *(private)* | Script インスタンス追加ダイアログを描画（ポップアップ外ファイルピッカーと連携） |

## 動作

- ツリー上でクリックすると `selectedInstance` が更新され、`PropertiesPanel` / `ViewportPanel` にリアルタイム反映される
- 通常クリックと Ctrl クリックは次回 Shift 選択のアンカーを更新する。Shift クリックは、そのフレームで展開されている可視行の深さ優先順にアンカーからクリック先までを両端込みで選択する。Ctrl+Shift では既存選択へ重複なく追加する
- アンカーが削除済み、別 Workspace、または折りたたみ内で非表示の場合、Shift クリックはクリック先だけへ安全にフォールバックする
- 右クリックの「子をすべて選択」は対象自身や孫を含めず、直下の子だけを Explorer 表示順で選択する。選択変更は Undo/Redo やシーンの dirty 状態へ影響しない
- インスタンスの追加・削除・親変更は `m_history` 経由で Undo 対応
- Script 追加時はダイアログで新規ファイル作成 or 既存ファイル選択を選べる（`m_doPick` / `m_pickExisting` フラグでポップアップ外ファイルピッカーを遅延実行）
- 右クリックの「グループ化」は選択項目を Model / Folder / Tool または Insert Object の各種コンテナへまとめる。生成と親変更は `GroupInstancesCommand` の1 Undo単位で処理し、Spatial 子孫のワールド姿勢を保持する。

## 依存関係

- `EditorPanel`, `Workspace`, `Instance`, `CommandHistory`, ImGui
