# Model

`include/Instances/Model.hpp`

複数の子オブジェクトをグループ化する軽量コンテナ。独自のロジックは持たず、`Spatial` の変換を共有グループの基点として使う。

## 継承

`Instance` → `Spatial` → `Model`

## メンバ変数

| プロパティ | 説明 |
|---|---|
| `PrimaryCube` | 原点とみなす子孫の `BaseCube`（Workspace相対パス文字列で保存、エディターはPickで指定、`Tool.Handle` と同じ方式）。設定するとModelの `Position`/Pivotがその座標になる。未設定なら従来どおり重心 |

## メソッド

| メソッド | 説明 |
|---|---|
| `GetClassName()` | `"Model"` を返す |
| `IsA(className)` | `"Model"`, `"Spatial"`, `"Instance"` に対して true |
| `getPrimaryCube()` | 解決済みのPrimaryCube（未設定・未解決・子孫でない場合は `nullptr`） |
| `syncPivotToPrimaryCube()` | 原点をPrimaryCubeのワールド座標へ更新（子のワールド姿勢は不変）。編集中は `main.cpp` が毎フレーム、Play中は `Physics::update` 経由の `syncPivotToCentroid()` が呼ぶ |
| `getPivotCFrame()` | PrimaryCubeがあればその座標、なければ子孫BaseCubeの重心 |

## 依存関係

- `Spatial`

## 使われる場所

- `User::character` がキャラクターモデルのルートとして `Model` を使用
- `User::spawnCharacter()` で `Model` の下に `Cube`（胴体・頭・腕・脚）をぶら下げる
