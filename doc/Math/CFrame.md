# CFrame

`include/Math/CFrame.hpp`

座標フレーム。位置と回転を一体で管理する変換オブジェクト（Unity の Transform に相当）。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Position` | `Vector3` | ワールド座標の位置 |
| `Rotation` | `Quaternion` | ワールド空間での回転 |

## メソッド

| メソッド | 説明 |
|---|---|
| `toMatrix4()` | OpenGL 用の 4×4 行列へ変換 |
| `FromMatrix4(m)` | 4×4 行列から構築 |
| `operator*` | 2 つの CFrame を合成（親子変換） |
| `pointToWorld(localPoint)` | ローカル座標 → ワールド座標変換 |

## 依存関係

- `Vector3`
- `Quaternion`
- `Matrix4`

## 使われる場所

- `Spatial::cframe`（すべての 3D オブジェクトの変換）
- `Renderer` で `toMatrix4()` を呼んでシェーダーへ渡す
- `Physics` で PhysX アクターと同期するときに参照される
