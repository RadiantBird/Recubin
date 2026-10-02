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

- `Cube::draw()` が子の `Decal` を検索し、その `TextureID` でテクスチャをオーバーライドする。BaseCube 直下に無い面は、参照中の [Material](Material.md) の子 `Decal`/`Texture` で補う（直下が優先）。Material の子の Decal は、その Material を参照する全 BaseCube の対応面へ投影される
- `LuauEngine` のバインディングで `TextureID` と `Face` を Luau スクリプトから操作可能
- Editor で選択すると、直接親の `Cube` 上の対象 `Face` の外縁4辺を選択色で表示する。親が [Material](Material.md) の Decal は、その Material を参照する全 Cube の対応面を表示する。親が `Cube`/Material 以外の Decal はこの補助表示を行わない
