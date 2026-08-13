# BillboardGui

`include/Instances/BillboardGui.hpp`

3D ワールド上の位置に配置され、常にカメラの方を向くように描画される GUI パネル。`ProximityPrompt` の描画基盤としても使われる。

## 継承
`Instance` → `WorldGuiObject` → `BillboardGui`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `Mode` | `BillboardMode` | `Parallel`（カメラ平面に平行）/ `Focus`（カメラ方向を向く） |
| `SizeMode` | `BillboardSizeMode` | `Screen`（画面上のサイズを維持）/ `World`（ワールド単位で投影） |
| `Offset` | `Vector3` | 親オブジェクトのローカル座標での表示位置オフセット |

`Screen`が既定値で、従来どおり`Size`を画面ピクセルとして扱う。`World`では`Size`をワールド単位として扱い、カメラから離れるほど画面上の表示が小さくなる。`World`時の子GUIも同じ投影倍率で縮小される。

Luauからは次のように設定できる。

```lua
billboard.SizeMode = "World"
billboard.Offset = Vector3.new(0, 1, 0)
```

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
