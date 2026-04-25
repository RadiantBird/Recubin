# Model

`include/Instances/Model.hpp`

複数の子オブジェクトをグループ化する軽量コンテナ。独自のロジックは持たず、`Spatial` の変換を共有グループの基点として使う。

## 継承

`Instance` → `Spatial` → `Model`

## メンバ変数

追加メンバなし（`Spatial` をそのまま継承）

## メソッド

| メソッド | 説明 |
|---|---|
| `GetClassName()` | `"Model"` を返す |
| `IsA(className)` | `"Model"`, `"Spatial"`, `"Instance"` に対して true |

## 依存関係

- `Spatial`

## 使われる場所

- `User::character` がキャラクターモデルのルートとして `Model` を使用
- `User::spawnCharacter()` で `Model` の下に `Cube`（胴体・頭・腕・脚）をぶら下げる
