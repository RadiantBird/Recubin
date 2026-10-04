# CelestialBody

`include/Instances/CelestialBody.hpp`

[Sun](Sun.md) / [Moon](Moon.md) の共通基底。空に見える円盤だけを担う軽量な `Instance`（`Spatial` でも `BaseCube` でもない）。`Workspace` が `getRenderCelestialBodies()` に登録し、`Renderer` が毎フレームカメラ基準で描く。

## 継承

`Instance` → `CelestialBody` → `Sun` / `Moon`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Color` | `Color4` | 円盤の色 |
| `Distance` (`Distance`) | `float` | **見かけの距離**（既定1000、範囲 10〜100000、`clampLua()`）。円盤は常にカメラから一定距離 `SKY_RENDER_DISTANCE`(1000) に置き、直径を `baseDiameter() × 1000 / Distance` にする。大きいほど角度的に小さく見える。`Distance = 1000` が従来の見た目（Sun 200 / Moon 150） |
| `GlowIntensity` | `float` | **逆光（グロー）**: 円盤の周りの柔らかい光のにじみの強さ（既定0 = 無効、範囲 0〜4） |
| `GlowRadius` | `float` | にじみが円盤の**半径の何倍**まで広がるか（既定6、範囲 1〜60） |
| `VeilIntensity` | `float` | **逆光（ベール）**: カメラが天体の方向を向くほど強まる画面全体の白い霞の強さ（既定0 = 無効、範囲 0〜1） |
| `VeilFalloff` | `float` | ベールが天体の方向からの角度でどれだけ急に減るか。大きいほど天体のごく近くだけに効く（既定6、範囲 1〜64） |
| `SpikeIntensity` | `float` | **光条（回折スパイク）**: 円盤から放射状に伸びる細い光の筋の強さ（既定0 = 無効、範囲 0〜4） |
| `SpikeCount` | `int` | 筋の本数。4 = 十字、6/8 = 星形（既定4、範囲 2〜16） |
| `SpikeLength` | `float` | 筋の長さ（円盤の**半径の何倍**か。既定12、範囲 1〜60） |
| `SpikeRotation` | `float` | 筋の回転（度。既定0） |
| `HorizonFade` | `bool` | 真なら、天体が地平線の下に沈むにつれて上記の効果を消す（既定true）。地平線の無いシーン（宇宙など）では偽にする |

## 逆光・光条の描画

強さが0の効果は描かない（既定はすべて無効なので、有効にしない限り見た目は変わらない）。`sky_flare` シェーダー（`shaders/sky_flare_vertex.glsl` / `sky_flare_fragment.glsl`、`Renderer_Sky.cpp` の `renderSkyFlares`）が、不透明物・地形・雲・パーティクルの描画後、選択アウトラインとポストエフェクトの前に重ねる（深度は書かない）。地平線の下は `HorizonFade` で消せる。

### 遮蔽

ハードウェアの深度テストは使わず、**ビューポートの深度をテクスチャへコピー**（`m_skyDepthTex`、`updateSkySceneDepth`）してシェーダーが読む。深度は投影行列の `m[10]`/`m[14]` で視線方向の距離へ戻し、「天体の円盤の最前面より手前（円盤半径の 1.15 倍）にある物体」を天体を隠す物体とみなす。円盤自身（深度に書き込まれる球）や遠景の Skybox は隠す物体にならない。円盤と同じ制約で、天体（カメラから 1000stud）より遠い物体は遮らない。深度のコピーが失敗する環境（フォーマット不一致など）では、警告を1回出して遮蔽なしで描く。

頂点シェーダーが天体の円盤の範囲を 5×5 でサンプルして「円盤が隠れていない割合」`vVisibility`（0〜1）を求め、フラグメントシェーダーは各画素の遮蔽率（5点平均で縁を柔らかくした値）を使う。

| 効果 | 描き方 | 物体との関係 |
|---|---|---|
| グロー | 天体の方向（円盤と同じ位置）に置いた、円盤半径の `GlowRadius` 倍の大きさのカメラ向きビルボード（加算）。円盤の縁から `GlowRadius` で0になる滑らかな減衰。色は天体の `Color`、全体の強さは `Color.a` と地平線フェードの積 | 物体に隠れた画素へも少しにじむが、その強さは `GLOW_BLEED`(0.35) × 天体が見えている割合。**天体が完全に隠れていれば物体の上のグローは0**（物体を貫通しない）。物体の縁の外側の空には従来どおりグローが出る |
| 光条 | 円盤半径の `SpikeLength` 倍の大きさのビルボード（グローと同じ1枚、加算）に、`SpikeCount` 本の筋が等間隔に放射し、先へ行くほど細く暗くなる | 手前の物体に隠れた画素には描かない（遮られる） |
| ベール | 画面全体を覆うクアッド（加算）で、ピクセルごとの視線方向と天体の方向の内積を `VeilFalloff` 乗して強さにする。色は天体の色を白寄りにした値。カメラが天体から大きく外れた向きのときは描かない | 画面全体の霞なので画素ごとの遮蔽はしないが、天体が隠れているほど弱い（完全に隠れれば0） |

## メソッド

| メソッド | 説明 |
|---|---|
| `baseDiameter()` | `Distance = 1000` のときの直径（純粋仮想。Sun 200 / Moon 150） |
| `apparentDiameter()` | 実際に描く直径。`Distance` が非有限・範囲外なら範囲内へ丸める |
| `hasFlareEffect()` | 逆光・光条のいずれかの強さが0より大きいか |
| `registerSchema(className, leading)` | Sun/Moon 共通のスキーマ（Color, Distance, 逆光, 光条）を `leading` の後ろに続けて登録する |
| `IsA(className)` | `"CelestialBody"` に対して true（Workspace への登録に使う） |
| `setProperty(name, value)` | 旧形式（BaseCube 派生だった頃）の互換。旧 BaseCube 系プロパティを無視し（インスタンスごとに1回だけ `RCBN_LOG`）、旧 `Size` を `Distance` へ換算する |

## 依存関係

- `Instance`, `Color4`, `Logger`, `PropertyRegistry`
- `Workspace`（登録）、`Renderer`（円盤と逆光・光条の描画）

## 継承クラス

`Sun`, `Moon`
