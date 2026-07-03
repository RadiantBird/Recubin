# TriangularPrism

`include/Instances/TriangularPrism.hpp`

三角柱形状の描画可能プリミティブ。半径0.5の円に内接する正三角形（頂点 A/B/C）を上下に配置し、上面・底面・3つの側面（AB/BC/CA）で構成する。`BaseCube` を継承し、物理形状は凸メッシュ（ConvexMesh）。

## 継承

`Instance` → `Spatial` → `BaseCube` → `TriangularPrism`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `defaultTextureID` | `static unsigned int` | クラス共有のデフォルトテクスチャ ID |
| `s_VAO` / `s_VBO` / `s_EBO` | `static unsigned int` | 全インスタンス共有の頂点配列・バッファ |
| `s_IndexCount` | `static int` | インデックスバッファの総数 |

## メソッド

| メソッド | 説明 |
|---|---|
| `TriangularPrism(Pos, Sz)` | 初回のみ `initGeometry()` を呼びジオメトリを生成 |
| `draw(modelLoc, shaderProgram)` | Top/Bottom/AB面(Front)/BC面(Back)/CA面(Right) の5領域を個別テクスチャで描画 |
| `IsA(className)` | `"TriangularPrism"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `clone()` | 位置・サイズ・色等をコピーし子を再帰複製 |
| `getPhysicsShape()` | `PhysicsShape::ConvexMesh` を返す |
| `getConvexVertices()` | A/B/C の上下6頂点を PhysX 用に列挙 |

## フロー

```
initGeometry() (初回のみ)
  → トップ三角 (法線+Y) / ボトム三角 (法線-Y, 逆巻き)
  → 各辺 A-B, B-C, C-A ごとに外向き法線を計算しクアッド生成
  → インデックス領域: [0,3)=Top [3,6)=Bottom [6,12)=AB(Front) [12,18)=BC(Back) [18,24)=CA(Right)

draw()
  → Top/Bottom/Front/Back/Right のデカールテクスチャを取得し、対応領域を glDrawElements
```

## 依存関係

- `BaseCube`, `Named`
- OpenGL（GLEW）

## 継承クラス

なし
