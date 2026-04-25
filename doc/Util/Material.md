# Material

`include/Util/Material.hpp`

オブジェクトの物理マテリアルプロパティ（摩擦・反発係数）を定義する。

## 列挙型

```cpp
enum MaterialType { Plastic, Wood, Metal, Stone }
```

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `type` | `MaterialType` | マテリアル種別 |
| `staticFriction` | `float` | 静止摩擦係数 |
| `dynamicFriction` | `float` | 動摩擦係数 |
| `restitution` | `float` | 反発係数（弾性） |

## メソッド

| メソッド | 説明 |
|---|---|
| `GetDefault(MaterialType)` | 種別に対応したプリセット値を返すファクトリ |

## 依存関係

なし

## 使われる場所

- `BaseCube::material` に保持される
- `Physics::createActor()` で PhysX の `PxMaterial` キャッシュ参照に使用
