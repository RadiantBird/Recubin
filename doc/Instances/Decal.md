# Decal

`include/Instances/Decal.hpp`

キューブの特定面にテクスチャを貼り付けるオブジェクト。`Cube` の子として配置する。

## 継承

`Instance` → `Decal`

## 列挙型

```cpp
enum Face { Front=0, Back=1, Top=2, Bottom=3, Right=4, Left=5 }
```

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `TextureID` | `unsigned int` | OpenGL テクスチャ ID |
| `face` | `Face` | 適用する面 |

## メソッド

| メソッド | 説明 |
|---|---|
| `GetClassName()` | `"Decal"` を返す |
| `IsA(className)` | `"Decal"`, `"Instance"` に対して true |
| `setProperty(name, value)` | YAML デシリアライズ用 |

## 依存関係

- `Instance`

## 使われる場所

- `Cube::draw()` が子の `Decal` を検索し、その `TextureID` でテクスチャをオーバーライドする
- `LuauEngine` のバインディングで `TextureID` と `Face` を Luau スクリプトから操作可能
