# Sphere

`include/Instances/Sphere.hpp`

球形状の描画可能プリミティブ。`BaseCube` を継承し、立方体の6面を球面上に投影・正規化した頂点（16分割/面）を静的に共有する。物理形状は `PhysicsShape::Sphere`。`Sun`/`Moon` の基底としても使われる。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Sphere`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `defaultTextureID` | `static unsigned int` | クラス共有のデフォルトテクスチャ ID |
| `s_VAO` / `s_VBO` / `s_EBO` | `static unsigned int` | 全インスタンス共有の頂点配列・バッファ |
| `s_IndexCount` | `static int` | インデックスバッファの総数 |
| `s_FaceIndexCount` | `static int` | 1面あたりのインデックス数（`draw()` のオフセット計算に使用） |

## メソッド

| メソッド | 説明 |
|---|---|
| `Sphere(Pos, Sz)` | 初回のみ `initGeometry()` を呼びジオメトリを生成 |
| `draw(modelLoc, shaderProgram)` | `Face` enum の6面順（Front, Back, Top, Bottom, Right, Left）でループしデカールテクスチャを切替描画 |
| `IsA(className)` | `"Sphere"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `clone()` | 位置・サイズ・色等をコピーし子を再帰複製 |
| `getPhysicsShape()` | `PhysicsShape::Sphere` を返す |

## フロー

```
initGeometry() (初回のみ)
  → キューブの各面 (法線, right, up) を 16x16 グリッドで分割
  → 各頂点をキューブ面上の位置から正規化 → 半径0.5の球面へ投影
  → Face enum の順 (Front,Back,Top,Bottom,Right,Left) で連続領域として VBO/EBO へ格納

draw()
  → for i in 0..6: getDecalTexture((Face)i, defaultTextureID) → glDrawElements(オフセット = i * s_FaceIndexCount)
```

## 依存関係

- `BaseCube`, `Named`
- OpenGL（GLEW）

## 継承クラス

- `Sun`（太陽表現）
- `Moon`（月表現）
