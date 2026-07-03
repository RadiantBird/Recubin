# WorldGuiObject

`include/Instances/WorldGuiObject.hpp`

3D ワールド上の位置に紐付いて表示される GUI 要素の基底クラス。カメラの `view`/`projection` 行列でスクリーン座標へ射影してから ImGui で描画される（`ScreenGuiObject` とは異なり画面固定ではない）。

## 継承
`Instance` → `WorldGuiObject`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `Size` | `Vector2` | パネルサイズ（既定 200x100） |
| `NormType` | `Norm` | `Pixel`/`Scale` |
| `Active` | `bool` | 入力を受け付けるか |
| `Visible` | `bool` | 表示/非表示 |
| `BackgroundColor` | `Color4` | 背景色（アルファ = 不透明度） |
| `ZIndex` | `int` | 描画順 |

## メソッド
| メソッド | 説明 |
|---|---|
| `getTransparency()`/`setTransparency(t)` | `BackgroundColor.a` を透明度として読み書き |
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用 |

## 依存関係
- `Vector2`, `Color4`
- `Renderer_GUI.cpp`（`renderWorldGui` で `m_lastView`/`m_lastProj` を用いてワールド→スクリーン射影後に描画）

## 継承クラス
- `BillboardGui`（常にカメラを向くパネル）
- `SurfaceGui`（Cube のフェイスに直接ベイクされる特殊系。3D パス側で描画されるため `renderWorldGui` からは除外）
