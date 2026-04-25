# Vector3

`include/Math/Vector3.hpp`

3D ベクトル。位置・方向・スケールの基本型。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `x` | `float` | X 成分 |
| `y` | `float` | Y 成分 |
| `z` | `float` | Z 成分 |

## メソッド

| メソッド | 説明 |
|---|---|
| `operator+` / `operator-` / `operator*` | 加減算・スカラー乗算 |
| `length()` | ベクトルの長さ |
| `normalize()` | 正規化したコピーを返す |
| `static Dot(a, b)` | 内積 |
| `static Cross(a, b)` | 外積 |
| `toString()` | 文字列変換 |

## 依存関係

なし（数学プリミティブ）

## 使われる場所

- `Spatial` の位置・サイズ
- `CFrame` の内部表現
- `Physics` のレイキャスト引数
- `User` のカメラ位置・移動方向
