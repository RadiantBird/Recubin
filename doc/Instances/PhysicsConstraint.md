# PhysicsConstraint

`include/Instances/PhysicsConstraint.hpp`

物理制約インスタンスに共通する基底クラス。`Instance` から派生し、`Rope`、`Rod`、`Spring`、`PrismaticConstraint`、`BallSocket`、`Weld`、`Motor`、`NoCollision` が継承する。

## 共通プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Enabled` | `bool` | `true` の場合のみネイティブ物理制約を有効化（既定`true`） |
| `Cube0` | `BaseCube` | 1つ目の接続対象 |
| `Cube1` | `BaseCube` | 2つ目の接続対象 |

`Enabled` を `false` にすると既存のネイティブ制約を解除し、`true` に戻すと条件が整い次第再生成する。

## BallSocket 固有プロパティ

`BallSocket` は各 joint frame 軸について、`Free`、`Limited`、`Locked` を設定できる。
ただし Box3D の球面 joint は「円錐制限1つ＋ツイスト制限1つ」しか持たないため、軸別の独立制限ではなく
次の近似で解く（Box3D 本体は改造しない）。

| プロパティ | 型 | 説明 |
|---|---|---|
| `AngularXMode` / `AngularYMode` / `AngularZMode` | `Free \| Limited \| Locked` | 各ローカル軸の回転モード（既定はすべて`Free`） |
| `AngularXMin` / `AngularXMax` | `float` | X軸の許可範囲（度） |
| `AngularYMin` / `AngularYMax` | `float` | Y軸の許可範囲（度） |
| `AngularZMin` / `AngularZMax` | `float` | Z軸の許可範囲（度） |

- **X / Y（円錐制限）**: どちらかが `Free` 以外なら有効。各軸の角度は `Locked`=0度、`Free`=180度、
  `Limited`=`min(|Min|, |Max|)` とし、X/Y のうち小さい方を円錐の半角にする（Min/Max が非対称でも対称に扱う）。
- **Z（ツイスト制限）**: `Free` 以外なら有効。`AngularZMin`〜`AngularZMax` をそのまま下限/上限にする。
  `Locked` でも Min/Max の範囲が使われるため、0度に固定するなら Min=Max=0 にする。

基準は world axis ではなく、BallSocket の `localFrameA/B`（Attachment または Motor6D の
bind frame）なので、Character の向きに依存しない。

BallSocket は接続 body の collision 設定を変更しない。接続 body 間の衝突を止める場合は、
別途 `NoCollision` を同じペアへ設定する。
