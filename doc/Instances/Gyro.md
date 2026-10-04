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

既定のCharacterリグ（`CharacterRig::buildDefaultRigParts`）の`RootGyro`は次の設定になる。

- `Enabled = false`で生成される。Humanoidが実行時（ラグドールからの復帰など）に有効化し、ラグドール開始時などに無効化する。
- X/Zは有効で目標0度、`MaxTorque`は上限なし。Rootを直立に保つ。
- **Yは無効**。向き（Yaw）はリグの`YawForce`（`MaintainVelocity`のトルク）が受け持ち、同じ回転軸を2つの制御が奪い合わないようにしている。
  Yの目標角（`setCharacterHeading`）は保存されるが、Yが無効の間はRootの向きを動かさない。

`setCharacterHeading`の方位は水平な方向ベクトルから直接算出するため、Rootの傾きをEuler分解した値には依存しない。
移動入力が無い場合はY目標を変更せず、最後に設定された方位を維持する。

Gyro単体（リグ以外）では、軸ごとに`Enabled`を有効にすれば、その軸のworld基準の角度制御が働く。
`MaxTorque`が十分大きいと外乱は1ステップで打ち消されるため、挙動を観察する場合は有限のトルクを使う。
