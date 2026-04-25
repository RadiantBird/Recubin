# Quaternion

`include/Math/Quaternion.hpp`

クォータニオンによる回転表現。オイラー角よりジンバルロックが発生しない。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `w` | `float` | 実部 |
| `x`, `y`, `z` | `float` | 虚部（回転軸成分） |

## メソッド

| メソッド | 説明 |
|---|---|
| `fromAxisAngle(axis, angleDegrees)` | 軸・角度からクォータニオン生成 |
| `FromRotationMatrix(m[16])` | 4×4 行列から変換 |
| `operator*` | クォータニオン合成 |
| `rotate(v)` | ベクトルをこの回転で変換 |
| `getRight()` / `getUp()` / `getForward()` | ローカル基底ベクトルを取得 |
| `Slerp(a, b, t)` | 球面線形補間 |
| `LookRotation(forward, up)` | 方向ベクトルから回転生成 |

## 依存関係

- `Vector3`

## 使われる場所

- `CFrame::Rotation`（Spatial の回転）
- `User::camera` のカメラ向き
- `Matrix4::FromQuaternion()` で行列変換
