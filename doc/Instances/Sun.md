# Sun

`include/Instances/Sun.hpp`

太陽。空に見える円盤であり、同時に**平行光源の向きの唯一の正**。`BaseCube` ではなく軽量な `Instance`（`CelestialBody` 派生）で、物理・Material 参照・ギズモの対象にならない。新規 Workspace には既定で1つ挿入される。

## 継承

`Instance` → [`CelestialBody`](CelestialBody.md) → `Sun`

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Angle` | `Vector2` | `(方角, 高度)`（度）。方角は北0°・東90°の時計回り（spec: +Xが北、+Zが東）、高度は水平0°・天頂90°・負で地平線の下。既定 `(135, 35.26)`（旧 `Lighting.Direction (1,-1,-1)` が指していた位置）。非有限値は拒否（`RCBN_ERROR`）。高度は [-90,90] に丸めて使う |
| `Color` | `Color4` | 円盤の色（既定 `(1, 0.95, 0.8, 1)`） |
| `Distance` | `float` | 見かけの距離（[CelestialBody](CelestialBody.md)）。基準直径は 200stud |
| 逆光・光条 | — | `GlowIntensity` / `GlowRadius` / `VeilIntensity` / `VeilFalloff` / `SpikeIntensity` / `SpikeCount` / `SpikeLength` / `SpikeRotation` / `HorizonFade`（[CelestialBody](CelestialBody.md)）。既定はすべて無効 |

## static ヘルパー

| 関数 | 説明 |
|---|---|
| `directionFromAngle(Vector2)` | 太陽方向の単位ベクトル。`(cos el·cos az, sin el, cos el·sin az)`。非有限値は既定角へ丸める |
| `lightDirectionFromAngle(Vector2)` | 光の進行方向（太陽方向の逆）。従来の `Lighting.Direction` と同じ規約 |
| `angleFromDirection(Vector3)` | 方向ベクトルから `(方角[0,360), 高度[-90,90])`。零ベクトルは既定角 |
| `angleFromLegacyScalar(float)` | 旧スカラー `Angle`（方向 `(0, sin a, cos a)`）を Vector2 へ換算 |
| `defaultAngle()` | 既定角 |

## 光源としての役割

```
Renderer::renderViewport (毎フレーム)
  → Workspace::getRenderCelestialBodies() から最初の Sun を取得
  → あれば lightDirection = Sun::lightDirectionFromAngle(Angle)
        影のライト空間行列とメインパスの lightDir uniform の両方に同じ方向を使う
        強さ・色は Lighting.Brightness / Lighting.Color（Lighting が無ければ 1.0 / 白）
  → 無ければ平行光源なし（brightness uniform を0、影パスをスキップ。環境光・Point/Spot のみ）
```

Sun が複数ある場合は**最初に登録された Sun だけ**が光の向きと Moon の位置を決める（他の Sun は自分の `Angle` で円盤だけ描かれる）。鏡面ハイライト（PBR）も同じ向きを使う。環境キューブマップには太陽の円盤は映らない。

## 描画

`Renderer::renderCelestialBodies`（`Renderer_Sky.cpp`）がカメラ位置 + 方向 × 1000 に、直径 `200 × 1000 / Distance` の unlit な球として毎フレーム描く（`teleportTo` はしない）。ビューポートではクリック選択できないので Explorer から選ぶ。

## 旧形式（BaseCube 派生）の互換

旧シーンの `Sun`/`Moon` ノードは読み込み時に自動移行される。旧 BaseCube 系プロパティ（Position/Anchored/Unlit/CanCollide/…）は無視、スカラー `Angle` は Vector2 へ、旧 `Size`（Vector3/float）は見かけの大きさを保つ `Distance` へ換算（`Distance = 基準直径 × 1000 / Size`）。旧 `Lighting.Direction` を持ち Sun が無い Workspace には、その向きを引き継ぐ Sun が自動生成される（`SceneRuntime::migrateLegacyLightingDirection`）。

## 依存関係

- `CelestialBody`, `Named`, `PropertyRegistry`, `Vector2`/`Vector3`
- `Renderer`（光の向きと円盤描画の消費側）、`SceneRuntime`（既定生成と旧 Direction の引き継ぎ）

## 継承クラス

なし
