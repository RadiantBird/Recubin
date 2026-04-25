# Cube

`include/Instances/Cube.hpp`

描画可能なキューブ。`BaseCube` に OpenGL レンダリング機能を追加する。

## 継承

`Instance` → `Spatial` → `BaseCube` → `Cube`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `defaultTextureID` | `static unsigned int` | クラス共有のデフォルトテクスチャ ID |

## メソッド

| メソッド | 説明 |
|---|---|
| `draw(modelLoc, shaderProgram)` | OpenGL でこのキューブを 1 フレーム描画 |
| `GetClassName()` | `"Cube"` を返す |
| `IsA(className)` | `"Cube"`, `"BaseCube"`, `"Spatial"`, `"Instance"` に対して true |

## 描画の仕組み

`draw()` は以下の手順で動作する：

1. `cframe.toMatrix4()` でモデル行列を計算
2. `Size` でスケール行列を適用
3. `shaderProgram` の `u_model` ユニフォームに転送
4. `Color` を `u_color` に転送
5. 子に `Decal` があればそのテクスチャを使用
6. `glDrawElements()` で描画

## 依存関係

- `BaseCube`, `Matrix4`
- OpenGL（GLEW）

## 使われる場所

- `Renderer::renderScene()` で Workspace 内の全 Cube を走査して `draw()` を呼ぶ
