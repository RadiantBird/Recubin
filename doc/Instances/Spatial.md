# Spatial

`include/Instances/Spatial.hpp`

3D トランスフォーム（位置・回転・サイズ）を持つ Instance。シーン上に配置できるすべてのオブジェクトの基盤。

## 継承

`Instance` → `Spatial`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `cframe` | `CFrame` | 位置と回転を統合した座標フレーム |
| `Size` | `Vector3` | オブジェクトのサイズ |
| `Position` | `Vector3&` | 互換用参照。永続変更は setter を使用 |
| `Rotation` | `Quaternion&` | 互換用参照。永続変更は setter を使用 |

## メソッド

| メソッド | 説明 |
|---|---|
| `GetClassName()` | `"Spatial"` を返す |
| `IsA(className)` | `"Spatial"` と `"Instance"` に対して true |
| `setProperty(name, value)` | `Position` / `Rotation` を YAML から設定 |
| `getWorldCFrame()` | 最近傍の Spatial 親（非Spatialは透過）からワールド姿勢を合成 |
| `setCFrame()` / `setPosition()` / `setRotation()` | ローカル姿勢を変更。Quaternion は検査・正規化し、子孫のワールド姿勢を保持 |
| `setWorldCFrame()` | ワールド姿勢を親基準のローカル姿勢へ変換 |
| `getCoordinateParent()` | 最近傍 Spatial 祖先を返す |

## 依存関係

- `Instance`
- `CFrame`, `Vector3`, `Quaternion`

## 継承クラス

- `BaseCube`（物理対応キューブ）
- `Sound`（3D 空間オーディオ）
- `Model`（グループコンテナ）
