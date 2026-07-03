# Lighting

`include/Instances/Lighting.hpp`

シーン全体の平行光源（ディレクショナルライト）設定を保持する `Instance`。Workspace 内に1つ配置され、`Renderer` がシャドウパス・メインパスの両方でこれを参照する。`Spatial` を継承しないシーン設定オブジェクト。

## 継承

`Instance` → `Lighting`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `lightDir` | `Vector3` | 光の方向ベクトル（既定 `(1,-1,-1)`）。PropertyRegistry範囲: 各成分 -1.0〜1.0 |
| `brightness` | `float` | 明るさ（既定1.0、範囲: 0.0〜5.0, `clampLua()`） |
| `lightColor` | `Color4` | 光の色（既定: 白） |

## メソッド

| メソッド | 説明 |
|---|---|
| `Lighting()` | `Instance("Lighting")` で初期化 |
| `getClassName()` | `"Lighting"` を返す |
| `IsA(className)` | `"Lighting"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` で `Direction`/`Brightness`/`Color` を反映、未処理なら `Instance::setProperty` |
| `clone()` | `PropertyRegistry::cloneFields` で全フィールドを複製 |

## フロー（シャドウ・メインパスでの利用）

```
Renderer::renderScene() (毎フレーム)
  → findLightingInTree(workspace) で Workspace 木を走査し Lighting を検索

Shadow Pass (Lighting が見つかった場合)
  → ld = normalize(lighting->lightDir)
  → lightEye = -ld * 80, lightView = LookAt(lightEye, origin, up)
  → lightProj = Ortho(-80..80, -80..80, 0.1..400)
  → lightSpaceMatrix = lightProj * lightView
  → シャドウマップへ CastShadow な BaseCube/Terrain を描画

Main Pass
  → lighting があれば lightDir/brightness/lightColor をシェーダ uniform へ
  → 無ければ既定値 (1,-1,-1) / 1.0 / 白 にフォールバック
```

## 依存関係

- `Instance`, `Vector3`, `Color4`, `PropertyRegistry`
- `Renderer`（シャドウ行列計算・メインパス uniform の消費側）

## 継承クラス

なし
