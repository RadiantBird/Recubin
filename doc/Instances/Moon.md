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
| `Phase` | `float` | **満ち欠け**。周期上の位置で、0 = 新月、0.25 = 上弦（右が明るい）、0.5 = 満月（既定）、0.75 = 下弦（左が明るい）、1 = 新月。範囲外の値は周期として折り返す（`wrappedPhase()`）。非有限値は満月扱い |
| `PhaseRotation` | `float` | 明暗境界の向き（度、画面基準。既定0）。0 のとき上弦は右、下弦は左が明るい |
| `Earthshine` | `float` | 影の部分がどれだけ見えるか（既定0.08、範囲 0〜1）。0 で完全に透明（空が見える）、1 で不透明 |

## 満ち欠けの描画

`Phase` が満月（0.5）のときは従来どおり発光球 1 個で描く（見た目は変わらない）。**満月以外**（`hasPhase()` が真）のときは、発光球をやめて、`sky_flare` シェーダー（[CelestialBody](CelestialBody.md) の遮蔽の節）の月の円盤モードで描く。

- カメラ向きの球として、法線と光の向きの内積に `smoothstep` をかけて明暗境界（terminator）を作る。光の向きは `Phase` から決まり、新月は奥から、上弦は右から、満月は手前から、下弦は左から当たる（`PhaseRotation` で回す）。
- 明るい側は `Color`、影の側は `Color × 0.35` で、不透明度は `Earthshine`。アルファブレンドなので、影の部分は空（Skybox）が透けて見える。縁は 0.97〜1.0 で滑らかに落とす。
- 手前の物体には他の効果と同じく遮られる。円盤の描画位置・大きさは発光球と同じ（カメラから 1000stud、直径 `150 × 1000 / Distance`）。
- `sky_flare` シェーダーが読み込めない環境では、満ち欠けなしの発光球で代用して月を消さない。
- 逆光・光条は円盤の後に重ねて描かれる。

月は常に太陽の反対側にあるため、満ち欠けは位置から自動では決まらず、`Phase` を直接指定する（Lua でゲーム内の日数から進めることもできる）。

## 描画（満月・満ち欠けなし）

`Renderer::renderCelestialBodies`（`Renderer_Sky.cpp`）が、カメラ位置から最初の Sun の方向の逆へ 1000stud の位置に、直径 `150 × 1000 / Distance` の unlit な球として描く。

## 旧形式の互換

[Sun](Sun.md) の「旧形式（BaseCube 派生）の互換」を参照。旧 `Size` は `Distance` へ換算される。

## 依存関係

- `CelestialBody`, `Named`, `PropertyRegistry`
- `Renderer`（位置決め・描画の消費側、最初の `Sun` の `Angle` に依存）

## 継承クラス

なし
