# CommandHistory

`include/Editor/CommandHistory.hpp`

Undo/Redo スタックを管理するクラスと、操作を表す `Command` インターフェース・各サブクラスの定義。

## Command インターフェース

```cpp
struct Command {
    virtual void execute() = 0;
    virtual void undo()    = 0;
};
```

## CommandHistory

| メソッド | 説明 |
|---|---|
| `execute(cmd)` | コマンドを即時適用して Undo スタックに積む（Redo スタックはクリア） |
| `record(cmd)` | 既に適用済みの変更を Undo スタックに記録する（インタラクティブ編集用） |
| `undo()` | Undo スタックから取り出して `undo()` を呼び、Redo スタックに積む |
| `redo()` | Redo スタックから取り出して `execute()` を呼び、Undo スタックに積む |
| `clear()` | 両スタックを空にする |
| `canUndo()` / `canRedo()` | スタックが空でないか確認 |

## Command サブクラス一覧

| クラス | 対象操作 |
|---|---|
| `AddInstanceCommand` | インスタンスを親に追加する |
| `RemoveInstanceCommand` | インスタンスを親から削除する |
| `MoveInstanceCommand` | インスタンスの親を変更する（ドラッグ&ドロップ） |
| `SetVec3Command` | `Position` または `Size` を変更する（`teleportTo` / `setSize`） |
| `SetColorCommand` | `Color` を変更する |
| `SetBoolCommand` | `Anchored` または `CanCollide` を変更する |
| `GizmoCommand` | ギズモ操作（position / size / rotation をまとめて保存） |

## GizmoState

```cpp
struct GizmoState {
    Vector3    position;
    Vector3    size;
    Quaternion rotation;
};
```

ギズモ操作開始時の状態を `GizmoState` として保存し、操作終了時に `GizmoCommand` として `record()` することで Undo 対応する。

## 使われる場所

- `EditorManager::m_history` として所有
- `hierarchyPanel`、`propertiesPanel`、`viewportPanel` に生ポインタで共有
- `handleEditorShortcuts()` で `Ctrl+Z` / `Ctrl+Shift+Z` によって `undo()` / `redo()` が呼ばれる
- `setWorkspace()` 時に `clear()` されてスタックがリセットされる

## 依存関係

- `BaseCube`（`SetVec3Command`, `SetColorCommand`, `SetBoolCommand`, `GizmoCommand` が操作対象）
- `Instance`（`AddInstanceCommand`, `RemoveInstanceCommand`, `MoveInstanceCommand` が操作対象）
