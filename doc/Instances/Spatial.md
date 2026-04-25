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
| `Position` | `Vector3&` | `cframe.Position` への参照エイリアス |
| `Rotation` | `Quaternion&` | `cframe.Rotation` への参照エイリアス |

## メソッド

| メソッド | 説明 |
|---|---|
| `GetClassName()` | `"Spatial"` を返す |
| `IsA(className)` | `"Spatial"` と `"Instance"` に対して true |
| `setProperty(name, value)` | `Position` / `Rotation` を YAML から設定 |

## 依存関係

- `Instance`
- `CFrame`, `Vector3`, `Quaternion`

## 継承クラス

- `BaseCube`（物理対応キューブ）
- `Sound`（3D 空間オーディオ）
- `Model`（グループコンテナ）
