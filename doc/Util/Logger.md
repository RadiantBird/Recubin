# Logger

`include/Util/Logger.hpp`

マクロベースのデバッグログシステム。ImGui コンソールへのフックを持つ。

## グローバル変数

| 変数 | 説明 |
|---|---|
| `g_logHook` | `ConsolePanel::pushLog` へのポインタ |
| `g_luauLogHook` | `ConsolePanel::pushLuauLog` へのポインタ |

## マクロ（DEBUG ビルド時のみ有効）

| マクロ | 用途 |
|---|---|
| `RCBN_LOG(msg)` | 通常ログ |
| `RCBN_WARN(msg)` | 警告ログ |
| `RCBN_ERROR(msg)` | エラーログ |

## 動作フロー

```
RCBN_LOG("message")
  └─ g_logHook が設定済みなら ConsolePanel::pushLog() を呼ぶ
  └─ 未設定なら stdout へ出力
```

## 依存関係

なし（マクロベース）

## 使われる場所

- エンジン全体で広く使用
- `main.cpp` 起動時に `g_logHook = &ConsolePanel::pushLog` を設定
- `LuauEngine` の `global_print_message` から `g_luauLogHook` を呼ぶ
