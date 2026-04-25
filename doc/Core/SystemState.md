# SystemState

`include/Core/SystemState.hpp`

エンジン全体で共有するグローバルフラグを保持するシングルトン構造体。

## 設計

Meyer's シングルトン（`static SystemState& get()`）

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `isPlaying` | `bool` | ゲーム実行中（Play モード） |
| `isPaused` | `bool` | 一時停止中 |
| `viewportFocused` | `bool` | ビューポートが入力フォーカスを持つ |
| `viewportZoomEnabled` | `bool` | ズーム操作が有効か |

## 依存関係

なし

## 使われる場所

| 読み取り元 | 用途 |
|---|---|
| `main.cpp` | Play/Stop トランジション判定・物理/スクリプト更新のゲート |
| `User::processInput()` | `viewportFocused` でカメラ入力を受け付けるか判断 |

| 書き込み元 | 用途 |
|---|---|
| `EditorManager::renderToolbar()` | Play/Pause/Stop ボタン押下時に `isPlaying` / `isPaused` を変更 |
| `ViewportPanel::onRender()` | マウスホバー時に `viewportFocused` を更新 |
