# ViewportFocusManager

`include/Editor/ViewportFocusManager.hpp`

ビューポートへの排他的フォーカス制御を提供するスレッドセーフシングルトン。複数ビューポートが存在する場合でも入力を受け取るのは常に 1 つのみにする。

## 設計

Meyer's シングルトン・コピー/ムーブ禁止

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `focusMutex` | `mutable mutex` | 排他制御 |
| `currentFocusedViewport` | `ViewportPanel*` | フォーカス中のビューポート |
| `focusGeneration` | `atomic<unsigned int>` | デバッグ用カウンタ |

## メソッド（クラスメンバ）

| メソッド | 説明 |
|---|---|
| `getInstance()` | シングルトンインスタンスを取得 |
| `onFocusViewport(vp)` | フォーカスをこのビューポートに移す（ロック付き） |
| `getFocusedViewport()` | 現在フォーカス中のビューポートを返す |
| `isFocused(vp)` | このビューポートがフォーカス中か |
| `clearFocus()` | フォーカスをクリア |
| `debugPrint()` | デバッグ情報を出力 |

## フリー関数ラッパー

```cpp
SetViewportFocus(vp)       // onFocusViewport() の糖衣構文
GetFocusedViewport()       // getFocusedViewport() の糖衣構文
ClearViewportFocus()       // clearFocus() の糖衣構文
IsViewportFocused(vp)      // isFocused() の糖衣構文
```

## フォーカス判定フロー

```
ViewportPanel::onRender()
  → マウスがホバー & クリック
    → SetViewportFocus(this)
      → ViewportFocusManager::onFocusViewport()

main.cpp:
  SystemState::viewportFocused = IsViewportFocused(viewportPanel)

User::processInput():
  if (!SystemState::viewportFocused) return; ← 入力を無視
```

## 依存関係

- `ViewportPanel`
- `<mutex>`, `<atomic>`
