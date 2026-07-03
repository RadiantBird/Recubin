# Easing

`include/Math/Easing.hpp`

キーフレーム間の補間カーブを表す純関数群。正規化された進捗 `t (0..1)` を受け取り、種類ごとに整形した進捗を返す。

## 列挙型

```cpp
enum class EasingType {
    Linear,       // 線形
    Quadratic,    // 2次
    Cosine,       // cos
    Sine,         // sin
    Exponential   // 指数
};
```

## メソッド

| メソッド | 説明 |
|---|---|
| `ease(type, t)` | `t` を `[0,1]` にクランプした上で `type` に応じた進捗値を返す |

## 補間式

| 種類 | 式 |
|---|---|
| `Linear` | `t` |
| `Quadratic` | `t * t` |
| `Cosine` | `(1 - cos(t * PI)) * 0.5` |
| `Sine` | `sin(t * PI * 0.5)` |
| `Exponential` | `2^(10 * (t - 1))` |

## 依存関係

- なし（`<cmath>` のみ）

## 使われる場所

- `Animation::evaluateTrack()` がキーフレーム間の補間に使用
- `AnimationEditorPanel` のキー追加時に `EasingType` を選択
