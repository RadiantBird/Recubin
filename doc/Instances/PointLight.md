# PointLight

`include/Instances/PointLight.hpp`

全方位に光を放つ点光源。`LightSource` に追加プロパティは持たず、親 `Spatial` の位置に配置される光源そのもの。

## 継承

`Instance` → `LightSource` → `PointLight`

## メンバ変数

なし（`LightSource` の `lightColor`/`brightness`/`range` のみ）

## メソッド

| メソッド | 説明 |
|---|---|
| `PointLight()` | `Named<PointLight, LightSource>("PointLight")` で初期化 |
| `IsA(className)` | `"PointLight"`, `"LightSource"`, `"Instance"` に対して true |
| `setProperty(name, value)` | `PropertyRegistry::loadProperty` を試行後 `LightSource::setProperty` にフォールバック（`PointLight` 自体は追加フィールド無し） |
| `clone()` | `PropertyRegistry::cloneFields` で `LightSource` 分も含め複製し、子を再帰複製 |

## 依存関係

- `LightSource`, `PropertyRegistry`

## 継承クラス

なし
