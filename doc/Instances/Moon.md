# Moon

`include/Instances/Moon.hpp`

月を表す `Sphere`（150stud）。独自の `Angle` は持たず、`Renderer` が `Sun` の角度から自動的に太陽の正反対側へ配置する。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Sphere` → `Moon`

## メンバ変数

なし（`Sphere`/`BaseCube` のプロパティのみ使用）

## メソッド

| メソッド | 説明 |
|---|---|
| `Moon()` | サイズ150stud, `Anchored=true`, `CanCollide=false`, `CastShadow=false`, `Unlit=true`, 青白色 `Color4(0.9,0.9,1.0,1)` で初期化 |
| `getClassName()` | `"Moon"` を返す |
| `IsA(className)` | `"Moon"`, `"Sphere"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `clone()` | 主要プロパティをコピーし子を再帰複製 |

## フロー

`Sun.md` のフロー節を参照。`Renderer::renderScene()` が毎フレーム `sunDir` の逆ベクトルへ `teleportTo` する。

## 依存関係

- `Sphere`, `PropertyRegistry`（プロパティ未登録・登録のみ空フィールドで `PropertyRegistry::registerClass("Moon", "Sphere", {})`）
- `Renderer`（位置更新・描画の消費側、`Sun` の角度に依存）

## 継承クラス

なし
