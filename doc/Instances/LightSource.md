# LightSource

`include/Instances/LightSource.hpp`

`PointLight`/`SpotLight` の共通基底。位置は自身の Spatial ではなく、親の `Spatial` のワールド CFrame から取得する（Roblox 風の「親にアタッチする光源」設計）。

## 継承

`Instance` → `LightSource`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `lightColor` | `Color4` | 光の色（既定: 白） |
| `brightness` | `float` | 明るさ（PropertyRegistry範囲: 0.0〜10.0, `clampLua()`） |
| `range` | `float` | 減衰半径（studs、既定16、範囲: 0.0〜200.0, `clampLua()`） |

## メソッド

| メソッド | 説明 |
|---|---|
| `LightSource(className)` | `Instance(className)` を呼び基底初期化 |
| `IsA(className)` | `"LightSource"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` で `Color`/`Brightness`/`Range` を反映、未処理なら `Instance::setProperty` |

## フロー

```
Renderer::renderScene()
  → collectLights() で Workspace 内の LightSource を収集
  → ls->Parent が Spatial なら getWorldCFrame() から position を取得
    (SpotLight の場合は Rotation.rotate(0,-1,0) で向きも計算)
  → uLights[] シェーダ配列へ position/color/brightness/range 等を転送（最大8灯）
```

## 依存関係

- `Instance`, `Color4`, `PropertyRegistry`
- `Renderer`（`uLights[]` uniform 配列へ集約する消費側）

## 継承クラス

- `PointLight`（全方位点光源）
- `SpotLight`（コーン状の指向性光源）
