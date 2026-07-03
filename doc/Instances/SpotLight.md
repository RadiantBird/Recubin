# SpotLight

`include/Instances/SpotLight.hpp`

コーン状に光を放つ指向性光源。方向は親 `Spatial` のワールド回転から「下向き」（ローカル `(0,-1,0)`）として計算される。

## 継承

`Instance` → `LightSource` → `SpotLight`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Angle` | `float` | コーン半角（度）。既定45、PropertyRegistry範囲: 1.0〜89.0, `clampLua()` |

## メソッド

| メソッド | 説明 |
|---|---|
| `SpotLight()` | `Named<SpotLight, LightSource>("SpotLight")` で初期化 |
| `IsA(className)` | `"SpotLight"`, `"LightSource"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` で `Angle` を反映、未処理なら `LightSource::setProperty` |
| `clone()` | `PropertyRegistry::cloneFields` で `Angle` と `LightSource` 分を複製し、子を再帰複製 |

## フロー

```
Renderer::renderScene()
  → ls->IsA("SpotLight") なら cosCutoff = cos(Angle * PI/180)
  → 親 Spatial の getWorldCFrame().Rotation.rotate(0,-1,0) を光源の向きとして uLights[].direction へ
```

## 依存関係

- `LightSource`, `PropertyRegistry`
- `Renderer`（`cosCutoff` 計算・コーン方向の消費側）

## 継承クラス

なし
