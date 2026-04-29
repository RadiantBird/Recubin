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
- インスタンスの追加・削除・親変更は `m_history` 経由で Undo 対応
- Script 追加時はダイアログで新規ファイル作成 or 既存ファイル選択を選べる（`m_doPick` / `m_pickExisting` フラグでポップアップ外ファイルピッカーを遅延実行）

## 依存関係

- `EditorPanel`, `Workspace`, `Instance`, `CommandHistory`, ImGui
