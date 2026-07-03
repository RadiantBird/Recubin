# GLFWInputBackend

`include/Core/GLFWInputBackend.hpp`

`IInputBackend` の GLFW 実装。`KeyCode` / `MouseButton`（エンジン共通の入力識別子、`include/Core/InputKey.hpp`）を GLFW の定数に変換して入力を読み取る。`User` はこのインタフェース経由でのみ入力を取得し、GLFW に直接依存しない。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_window` | `GLFWwindow*` | 対象ウィンドウ |
| `m_pendingScrollY` | `double` | コールバックで蓄積されたスクロール量 |
| `m_previousScrollCallback` | `GLFWscrollfun` | 既存のスクロールコールバック（ImGui 等）への連鎖用退避 |
| `s_instance` | `static GLFWInputBackend*` | GLFW コールバック用の自己ポインタ |

## メソッド

| メソッド | 説明 |
|---|---|
| `isKeyDown(key)` | `KeyCode` → GLFW キー定数に変換して `glfwGetKey()` を呼ぶ |
| `isMouseButtonDown(button)` | `MouseButton` → GLFW 定数に変換して `glfwGetMouseButton()` を呼ぶ |
| `getCursorPos(x, y)` | `glfwGetCursorPos()` のラッパー |
| `setCursorPos(x, y)` | `glfwSetCursorPos()` のラッパー（回転ドラッグ中の再センタリング用） |
| `setMouseCaptured(captured)` | カーソルの非表示・ロック（`GLFW_CURSOR_DISABLED`）を切り替え、対応環境では Raw Mouse Motion も有効化 |
| `consumeScrollDelta()` | 蓄積したスクロール量を返して内部カウンタを 0 にリセット |
| `scrollCallback(window, x, y)` | `static`。GLFW のスクロールコールバック。`m_pendingScrollY` に加算後、退避しておいた既存コールバックへ連鎖する |

## 入力フロー

```
GLFWInputBackend(window)
  → glfwSetScrollCallback() で自分のコールバックに差し替え、旧コールバックは退避

毎フレーム User::processInput()
  → isKeyDown() / isMouseButtonDown() / getCursorPos() で状態取得
  → consumeScrollDelta() でスクロール量を取り出す
```

## 依存関係

- GLFW
- `IInputBackend`（実装するインタフェース）, `KeyCode` / `MouseButton`（`InputKey.hpp`）

## 使われる場所

- `main.cpp` がウィンドウ生成後に `GLFWInputBackend` を構築し、`User` に渡す
- `User::processInput()` が `IInputBackend&` として参照し、GLFW を意識せず入力を読む
