# PhysicsConstraint

`include/Instances/PhysicsConstraint.hpp`

物理制約インスタンスに共通する基底クラス。`Instance` から派生し、`Rope`、`Rod`、`BallSocket`、`Weld`、`Motor`、`NoCollision` が継承する。

## 共通プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Enabled` | `bool` | `true` の場合のみネイティブ物理制約を有効化（既定`true`） |
| `Cube0` | `BaseCube` | 1つ目の接続対象 |
| `Cube1` | `BaseCube` | 2つ目の接続対象 |

`Enabled` を `false` にすると既存のネイティブ制約を解除し、`true` に戻すと条件が整い次第再生成する。
