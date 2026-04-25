# Matrix4

`include/Math/Matrix4.hpp`

4×4 変換行列。OpenGL に渡す列優先形式。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m[16]` | `float` | 列優先（column-major）の行列要素 |

## メソッド

| メソッド | 説明 |
|---|---|
| `operator*` | 行列同士の乗算 |
| `Translate(x, y, z)` | 平行移動行列を生成 |
| `Scale(x, y, z)` | スケール行列を生成 |
| `RotateX/Y/Z(degrees)` | 軸回転行列を生成 |
| `Perspective(fov, aspect, zNear, zFar)` | 透視投影行列 |
| `LookAt(eye, target, up)` | ビュー行列 |
| `FromQuaternion(q)` | クォータニオンから回転行列を生成 |

## 依存関係

- `Vector3`
- `Quaternion`

## 使われる場所

- `Renderer` でモデル・ビュー・プロジェクション行列を構築してシェーダーへ転送
- `CFrame::toMatrix4()` の出力型
- `ViewportPanel` の ImGuizmo 操作で使用
