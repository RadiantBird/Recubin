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
| `operator+` / `operator-` / 単項`operator-` | 加減算・反転 |
| `operator*` / `operator/` | スカラー倍・コンポーネント単位の乗除算 |
| `operator==` | 等価比較 |
| `length()` | ベクトルの長さ |
| `normalize()` | 正規化したコピーを返す |
| `set(x, y, z)` | userdata自身の成分を変更し、自身を返す |
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

## Luau bindingの所有権・再利用

Luauの`Vector3`はmutable userdataであり、`v:set(x, y, z)`と`v.x`／`v.y`／`v.z`
の代入は同じuserdataの内容を変更する。`set`は`self`を返す。

`Vector3.zero`は共有userdataではなく、アクセスごとに独立したゼロ値userdataを返す。
そのため、`local a = Vector3.zero`と`local b = Vector3.zero`は別オブジェクトになる。
一方、`local a = Vector3.zero; local b = a`は同じuserdataを参照する。

`+`、`-`、`*`、`/`および`normalize()`は既存どおり新しいVector3 userdataを返す。
Lua側で長寿命のuserdataを`set`により明示的に再利用することで、一時Vector3の生成とGCを
避けられる。
