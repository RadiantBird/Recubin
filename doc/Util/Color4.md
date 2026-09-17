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

PropertiesPanelでは編集の利便性のためRGBAを0〜255でも表示・入力できるが、入力時に
0〜1へ正規化される。シリアライズ、Luau、および`Color4`自身の値域は0〜1のfloatのまま。
