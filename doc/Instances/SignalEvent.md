# SignalEvent

`include/Instances/SignalEvent.hpp`

Luauスクリプトから任意個数・任意型の引数で発火できる汎用イベント（シグナル）インスタンス。`Instance.new("SignalEvent")`で生成し、`Fired:Connect(function(...) ... end)`で購読、`:Fire(...)`で発火する。既存の`Event`インスタンス（`Until`接続の一括切断専用）とは別の、汎用pub/sub用途。

## 継承
`Instance` → `SignalEvent`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Fired` | `shared_ptr<RCBNScriptSignal>` | `:Fire(...)`で渡された引数がそのまま渡されて発火するシグナル |

## メソッド

| メソッド | 説明 |
|---|---|
| `Fire(...)` | 任意個数・任意型の引数を`Fired`の全リスナーへ転送して発火する |

## 依存関係

- `RCBNScriptSignal` / `RCBNScriptConnection`（`Core/RCBNScriptSignal.hpp`）

## 継承クラス

なし
