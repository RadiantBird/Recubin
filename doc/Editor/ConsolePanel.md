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

ログ本文は二層構造で表示する。下層の `InputTextMultiline` は読み取り専用の選択・スクロールレイヤーで、文字とキャレットを透明にして範囲選択と Ctrl+C を提供する。上層は同じ内部 child の位置、クリップ、スクロールに合わせてログを重要度別カラーで描画する。`ImGuiInputTextFlags_ReadOnly` により、貼り付け、文字入力、削除、切り取りなどのユーザー入力でログ内容を変更することはできない。

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
