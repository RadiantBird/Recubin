# LiquidCube

`include/Instances/LiquidCube.hpp`

水などの液体ボリュームを表す `BaseCube`。物理コリジョンを持たず（`CanCollide=false`）、内部に侵入した `BaseCube` へ浮力を与える。描画は半透明ボックスとして単純化されている。

## 継承

`Instance` → `Spatial` → `BaseCube` → `LiquidCube`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Density` | `float` | 流体密度（浮力係数）。物体側の暗黙密度1.0を基準に `Density<1` で沈み `>1` で浮く目安（PropertyRegistry範囲: 0.0〜50.0） |

## メソッド

| メソッド | 説明 |
|---|---|
| `LiquidCube(Pos, Sz)` | `CanCollide=false`, `CastShadow=false`, 半透明の水色 `Color4(0.2,0.5,0.9,0.5)` で初期化 |
| `draw(modelLoc, shaderProgram)` | `Cube::s_VAO` を流用し面デカール処理なしで単純描画（`uvScale=(1,1)`, `isSurfaceGui=0`） |
| `IsA(className)` | `"LiquidCube"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` で `Density` を反映、未処理なら `BaseCube::setProperty` |
| `clone()` | 位置・サイズ・色等をコピーし `PropertyRegistry::cloneFields` で `Density` を複製、子を再帰複製 |

## フロー

```
draw()
  → Cube::s_VAO / Cube::defaultTextureID を流用
  → Color(半透明)のみ uniform に転送 → glDrawElements(36頂点の立方体)
```

浮力の適用ロジック自体は `Physics` 側の責務であり、`LiquidCube` は `Density` パラメータの保持と描画のみを担う。

## 依存関係

- `BaseCube`, `Cube`（`s_VAO`/`defaultTextureID` 流用）, `PropertyRegistry`
- OpenGL（GLEW）

## 継承クラス

なし
