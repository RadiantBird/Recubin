# Sun

`include/Instances/Sun.hpp`

太陽を表す巨大な `Sphere`（200stud）。`Angle` で公転角を指定し、`Renderer` が毎フレームカメラ位置を中心にその方向へ再配置する。無衝突・無影・無ライティングの発光体として描画のみを担う。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Sphere` → `Sun`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Angle` | `float` | 公転角（度）。0=+Z水平、90=天頂、180=-Z水平。PropertyRegistry範囲: 0.0〜360.0 |

## メソッド

| メソッド | 説明 |
|---|---|
| `Sun()` | サイズ200stud, `Anchored=true`, `CanCollide=false`, `CastShadow=false`, `Unlit=true`, 暖色 `Color4(1,0.95,0.8,1)` で初期化 |
| `getClassName()` | `"Sun"` を返す |
| `IsA(className)` | `"Sun"`, `"Sphere"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` で `Angle` を反映、未処理なら `Sphere::setProperty` |
| `clone()` | `Angle` を含む主要プロパティをコピーし子を再帰複製 |

## フロー（Sun/Moon 位置更新サイクル）

```
Renderer::renderScene() (毎フレーム)
  → Workspace 内から Sun/Moon インスタンスを検索
  → rad = Sun::Angle * PI/180
  → sunDir = (0, sin(rad), cos(rad))
  → Sun::teleportTo(cameraPosition + sunDir * 1000)
  → Moon が存在すれば Moon::teleportTo(cameraPosition - sunDir * 1000)  ※太陽の正反対側

Shadow Pass
  → Lighting::lightDir を基準にライト空間行列を計算（Sun位置とは独立、Lighting側の方向ベクトルを使用）
```

## 依存関係

- `Sphere`, `PropertyRegistry`
- `Renderer`（位置更新・描画の消費側）

## 継承クラス

なし
