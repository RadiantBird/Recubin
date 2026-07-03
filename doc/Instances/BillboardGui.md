# BillboardGui

`include/Instances/BillboardGui.hpp`

3D ワールド上の位置に配置され、常にカメラの方を向くように描画される GUI パネル。`ProximityPrompt` の描画基盤としても使われる。

## 継承
`Instance` → `WorldGuiObject` → `BillboardGui`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `Mode` | `BillboardMode` | `Parallel`（カメラ平面に平行）/ `Focus`（カメラ方向を向く） |

## メソッド
| メソッド | 説明 |
|---|---|
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用 |
| `clone()` | 自身と子を複製（`PropertyRegistry::cloneFields` で `WorldGuiObject` 分も含めて集約） |

## 依存関係
- `WorldGuiObject`, `Named`
- `Renderer_GUI.cpp`（`renderWorldGui` 内でワールド→スクリーン射影後に描画）

## 継承クラス
- `ProximityPrompt`（インタラクト促進 UI として特化）
