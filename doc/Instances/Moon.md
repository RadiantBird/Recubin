# Moon

`include/Instances/Moon.hpp`

月。空に見える円盤だけを担い、光源にはならない。`BaseCube` ではなく軽量な `Instance`（`CelestialBody` 派生）。独自の `Angle` は持たず、`Renderer` が**最初の Sun の反対側**へ描く（Sun が無ければ既定の Sun 角の反対側）。

## 継承

`Instance` → [`CelestialBody`](CelestialBody.md) → `Moon`

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Color` | `Color4` | 円盤の色（既定 `(0.9, 0.9, 1, 1)`） |
| `Distance` | `float` | 見かけの距離（[CelestialBody](CelestialBody.md)）。基準直径は 150stud |
| 逆光・光条 | — | Sun と同じ（[CelestialBody](CelestialBody.md)）。既定はすべて無効。効果の向きは月の描画方向（最初の Sun の反対側） |

## 描画

`Renderer::renderCelestialBodies`（`Renderer_Sky.cpp`）が、カメラ位置から最初の Sun の方向の逆へ 1000stud の位置に、直径 `150 × 1000 / Distance` の unlit な球として描く。

## 旧形式の互換

[Sun](Sun.md) の「旧形式（BaseCube 派生）の互換」を参照。旧 `Size` は `Distance` へ換算される。

## 依存関係

- `CelestialBody`, `Named`, `PropertyRegistry`
- `Renderer`（位置決め・描画の消費側、最初の `Sun` の `Angle` に依存）

## 継承クラス

なし
