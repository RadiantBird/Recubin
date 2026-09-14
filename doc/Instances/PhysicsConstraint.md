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

## BallSocket 固有プロパティ

`BallSocket` は各 joint frame 軸について、`Free`、`Limited`、`Locked` を独立して設定できる。

| プロパティ | 型 | 説明 |
|---|---|---|
| `AngularXMode` / `AngularYMode` / `AngularZMode` | `Free \| Limited \| Locked` | 各ローカル軸の回転モード（既定はすべて`Free`） |
| `AngularXMin` / `AngularXMax` | `float` | X軸の許可範囲（度） |
| `AngularYMin` / `AngularYMax` | `float` | Y軸の許可範囲（度） |
| `AngularZMin` / `AngularZMax` | `float` | Z軸の許可範囲（度） |

`Limited` は対応する Min/Max の範囲、`Locked` は bind pose の相対角度0度を許可する。
角度は world axis ではなく、BallSocket の `localFrameA/B`（Attachment または Motor6D の
bind frame）で評価されるため、Character の向きに依存しない。Box3D spherical joint の
solver が相対 quaternion の軸別 inequality/bilateral constraint を解き、毎 frame の
Euler角を直接補正しない。

BallSocket は接続 body の collision 設定を変更しない。接続 body 間の衝突を止める場合は、
別途 `NoCollision` を同じペアへ設定する。
