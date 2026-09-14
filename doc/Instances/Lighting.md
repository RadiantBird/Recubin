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
| `shadowDistance` (`ShadowDistance`) | `float` | シャドウを適用するカメラからの最大距離（既定160.0） |
| `shadowFadeDistance` (`ShadowFadeDistance`) | `float` | 最大距離手前からのフェード幅（既定20.0、0でハードカット） |

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
  → ld = normalize(lighting->lightDir)（無効値は警告して既定方向へフォールバック）
  → `ShadowDistance` を終端に practical split（3 cascade、linear/logarithmic混合 lambda=0.7）
  → 各カメラ frustum slice の8頂点をlight-spaceへ変換し、bounds + XY margin 8 / depth margin 32で cascadeごとのtight-fit Orthoを作成
  → cascadeごとにlight-space XY中心を `projectionWidth/2048`、`projectionHeight/2048` のtexel gridへsnap
  → `GL_TEXTURE_2D_ARRAY` の layer 0/1/2を切り替え、`CastShadow` と `ShadowMode` の共通判定を満たす BaseCube、および Terrain を各cascadeへ描画（24-bit深度、GL_NEAREST、手動3×3 PCF）。受け側biasは最小 0.00035、slope-scale 0.0012、書き込み側は `glPolygonOffset(1.0, 1.0)` とする

Main Pass
  → lighting があれば lightDir/brightness/lightColor をシェーダ uniform へ
  → view-space depthでcascadeを選択し、split近傍8%程度だけ隣接cascadeのPCF結果をblend
  → 無ければ既定値 (1,-1,-1) / 1.0 / 白 にフォールバック
```

## 依存関係

- `Instance`, `Vector3`, `Color4`, `PropertyRegistry`
- `Renderer`（シャドウ行列計算・メインパス uniform の消費側）

## 継承クラス

なし
