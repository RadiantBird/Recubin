# Truss

`include/Instances/Truss.hpp`

はしご。見た目・衝突形状は`Cube`と完全に同じ(Box、Decal/Texture対応込み)で、専用ジオメトリは持たない。`Humanoid`が`Physics::findOverlapping(*Root, "Truss")`で接触を検知し、接触中はW/Sで垂直移動、A/Dで水平ストレイフできるようになる(`Humanoid::ClimbSpeed`)。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Cube` → `Truss`

## メソッド

| メソッド | 説明 |
|---|---|
| `Truss(Pos, Sz, defaultTex)` | `Cube`のコンストラクタをそのまま継承(`using Named<Truss,Cube>::Named`) |
| `IsA(className)` | `"Truss"`, `"Cube"`, `"BaseCube"`, ... に対して true |
| `clone()` | 位置・サイズ・色等をコピーし子を再帰複製(`Cube::clone()`と同じパターン) |

## 依存関係

- `Cube`（描画・IsAチェーンをそのまま継承。Renderer.cppの`IsA("Cube")`分岐に自然に乗るため描画コード追加は不要）
- `Physics::findOverlapping`（接触判定）, `Humanoid::move`（登坂ロジック本体）

## 継承クラス

なし
