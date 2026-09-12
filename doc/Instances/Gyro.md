# Gyro

`include/Instances/Gyro.hpp`

1つの`BaseCube`へworld基準の角度制御を加える`PhysicsConstraint`。二つ目のendpointは持たない。
現行実装はBox3D backendを対象とする。

## 軸別プロパティ

X/Y/Zの各軸は次の設定を独立して持つ。

| 設定 | 説明 |
|---|---|
| `Enabled` | その軸の制御を有効にする |
| `TargetAngle` | world基準の目標角度（度） |
| `MaxTorque` | 適用できる最大トルク |
| `MaxAngularSpeed` | 目標角速度の上限（度/秒） |

Editor/YAML上の名前は軸名を先頭につけた`XEnabled`、`XTargetAngle`、`XMaxTorque`、
`XMaxAngularSpeed`形式で、Y/Zも同様である。旧`TargetRotation`、`Frequency`、
`DampingRatio`は読み替えず、エラーとして報告する。

## Character利用

Character用の共通設定はX/Zを0度へ固定して直立させ、Yを現在または入力方向の方位へ設定する。
方位は水平な方向ベクトルから直接算出するため、Rootの傾きをEuler分解した値には依存しない。
移動入力が無い場合はY目標を変更せず、最後に設定された方位を維持する。
