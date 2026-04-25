# Color4

`include/Util/Color4.hpp`

RGBA カラー表現（0〜1 の float 範囲）。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `r`, `g`, `b`, `a` | `float` | 各チャンネル（0.0〜1.0） |

## メソッド

| メソッド | 説明 |
|---|---|
| `FromRGB(r, g, b, a)` | 0〜255 の値から構築するファクトリ |
| `toString()` | 文字列変換 |

## 依存関係

なし

## 使われる場所

- `BaseCube::Color`（オブジェクト色）
- `LuauEngine` のバインディングで Luau スクリプトから読み書きされる
- `PropertiesPanel` のカラーピッカー
