# Cylinder

`include/Instances/Cylinder.hpp`

円柱形状の描画可能プリミティブ。`BaseCube` を継承し、独自の円柱ジオメトリ（32分割）を静的に一度だけ構築して全インスタンスで共有する。物理形状は凸メッシュ（ConvexMesh）。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Cylinder`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `defaultTextureID` | `static unsigned int` | クラス共有のデフォルトテクスチャ ID |
| `s_VAO` / `s_VBO` / `s_EBO` | `static unsigned int` | 全インスタンス共有の頂点配列・バッファ |
| `s_IndexCount` | `static int` | インデックスバッファの総数 |

## メソッド

| メソッド | 説明 |
|---|---|
| `Cylinder(Pos, Sz)` | 初回のみ `initGeometry()` を呼びジオメトリを生成 |
| `draw(modelLoc, shaderProgram)` | 上面・底面・側面（4分割）を面ごとに個別のデカールテクスチャで描画 |
| `IsA(className)` | `"Cylinder"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `clone()` | 位置・サイズ・色等をコピーし子を再帰複製 |
| `getPhysicsShape()` | `PhysicsShape::ConvexMesh` を返す |
| `getConvexVertices()` | 上下円周（32分割）の頂点を PhysX 用に列挙 |

## フロー

```
initGeometry() (初回のみ)
  → 上面キャップ (法線+Y) / 下面キャップ (法線-Y) を扇状に生成
  → 側面を4分割 (Right/Back/Left/Front) してクアッド生成
  → VAO/VBO/EBO へアップロード

draw()
  → Top/Bottom/Right/Back/Left/Front ごとに
    getDecalTexture(face, defaultTextureID) でテクスチャ切替 → glDrawElements
```

## 依存関係

- `BaseCube`, `Named`（CRTP で `getClassName()` を自動設定）
- OpenGL（GLEW）, PhysX（`PxVec3`）

## 継承クラス

なし
