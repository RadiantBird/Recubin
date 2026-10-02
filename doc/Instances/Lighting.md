# Lighting

`include/Instances/Lighting.hpp`

シーン全体の平行光源（太陽光）の強さ・色と影の設定を保持する `Instance`。Workspace 内に1つ配置され、`Renderer` がシャドウパス・メインパスの両方でこれを参照する。`Spatial` を継承しないシーン設定オブジェクト。

**光の向きは持たない。** 向きは Workspace の最初の [Sun](Sun.md)（`Sun.Angle`）が決める。Sun が無いと平行光源は出ない（環境光と Point/Spot のみ、影なし）。

## 継承

`Instance` → `Lighting`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `legacyDirection` | `std::optional<Vector3>` | 廃止された旧 `Direction`（光の進行方向）。旧形式 YAML を読んだときに引き継ぎ用に保持するだけで、保存・クローン・Lua 公開はしない。`SceneRuntime::migrateLegacyLightingDirection` が、Sun を持たない旧シーンに限りこの向きを引き継ぐ Sun を生成して消費する |
| `brightness` | `float` | 太陽光の明るさ（既定1.0、範囲: 0.0〜5.0, `clampLua()`） |
| `lightColor` | `Color4` | 太陽光の色（既定: 白） |
| `shadowDistance` (`ShadowDistance`) | `float` | シャドウを適用するカメラからの最大距離（既定160.0） |
| `shadowFadeDistance` (`ShadowFadeDistance`) | `float` | 最大距離手前からのフェード幅（既定20.0、0でハードカット） |

## メソッド

| メソッド | 説明 |
|---|---|
| `Lighting()` | `Instance("Lighting")` で初期化 |
| `getClassName()` | `"Lighting"` を返す |
| `IsA(className)` | `"Lighting"`, `"Instance"` に対して true |
| `setProperty(name, value)` | 旧 `Direction` は `legacyDirection` へ保持（不正値は警告して無視）。その他は `PropertyRegistry::loadProperty` で `Brightness`/`Color`/`ShadowDistance`/`ShadowFadeDistance` を反映、未処理なら `Instance::setProperty` |
| `clone()` | `PropertyRegistry::cloneFields` で全フィールドを複製（`legacyDirection` は複製しない） |

## フロー（シャドウ・メインパスでの利用）

```
Renderer::renderScene() (毎フレーム)
  → findLightingInTree(workspace) で Workspace 木を走査し Lighting を検索

Shadow Pass (Lighting と Sun が見つかった場合。Sun が無ければスキップ)
  → ld = Sun::lightDirectionFromAngle(Sun.Angle)（単位ベクトル。メインパスと同じ値）
  → `ShadowDistance` を終端に practical split（3 cascade、linear/logarithmic混合 lambda=0.7）
  → 各カメラ frustum slice の8頂点をlight-spaceへ変換し、bounds + XY margin 8 / depth margin 32で cascadeごとのtight-fit Orthoを作成
  → cascadeごとにlight-space XY中心を `projectionWidth/2048`、`projectionHeight/2048` のtexel gridへsnap
  → `GL_TEXTURE_2D_ARRAY` の layer 0/1/2を切り替え、`CastShadow` と `ShadowMode` の共通判定を満たす BaseCube、および Terrain を各cascadeへ描画（24-bit深度、GL_NEAREST、手動3×3 PCF）。受け側biasは最小 0.00035、slope-scale 0.0012、書き込み側は `glPolygonOffset(1.0, 1.0)` とする

Main Pass
  → Sun があれば lightDir = Sun の光の向き、brightness/lightColor は Lighting の値（Lighting が無ければ 1.0 / 白）
  → Sun が無ければ brightness uniform を 0 にして平行光源の寄与を消す（拡散・鏡面とも lightColor*brightness を掛けるため）
  → view-space depthでcascadeを選択し、split近傍8%程度だけ隣接cascadeのPCF結果をblend
```

## 依存関係

- `Instance`, `Vector3`, `Color4`, `PropertyRegistry`
- `Sun`（光の向き）、`Renderer`（シャドウ行列計算・メインパス uniform の消費側）、`SceneRuntime`（旧 Direction の引き継ぎ）

## 継承クラス

なし
