# ConsolePanel

`include/Editor/ConsolePanel.hpp`

C++ ログと Luau スクリプトログを 2 タブで表示するログコンソールパネル。

## 継承

`EditorPanel` → `ConsolePanel`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `logs` | `deque<string>` | C++ ログ（最大 512 件） |
| `scrollToBottom` | `bool` | 新規ログ追加時に自動スクロールするか |
| `filterBuf[256]` | `char` | C++ ログの検索フィルタ |
| `luauLogs` | `deque<string>` | Luau スクリプトログ（最大 512 件） |
| `luauScrollToBottom` | `bool` | Luau ログの自動スクロール |
| `luauFilterBuf[256]` | `char` | Luau ログの検索フィルタ |

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | 2 タブのログ表示ウィンドウを描画 |
| `clear()` | ログをクリア |
| `pushLog(msg)` | C++ ログを追加 |
| `pushLuauLog(msg)` | Luau ログを追加 |

## 他クラスとの接続

```
main.cpp 起動時:
  Logger::g_logHook     = &ConsolePanel::pushLog
  Logger::g_luauLogHook = &ConsolePanel::pushLuauLog

RCBN_LOG("msg")       → pushLog()     → logs deque
LuauEngine::print()   → pushLuauLog() → luauLogs deque
```

## 依存関係

- `EditorPanel`, ImGui
