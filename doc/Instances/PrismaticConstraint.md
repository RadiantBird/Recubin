# PrismaticConstraint

`include/Instances/PrismaticConstraint.hpp`

2つの`BaseCube`の相対移動を`Axis`方向のみに制限する直動拘束。Box3Dの`b3PrismaticJoint`を使う。モーター・ばねは持たない。`Attachment0/1`が無ければ各Cubeの中心がアンカー。

## 継承
`Instance` → `PhysicsConstraint` → `PrismaticConstraint`

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Cube0`/`Cube1` | `BaseCube`参照 | 接続対象（Workspace相対パス） |
| `Attachment0`/`Attachment1` | `Attachment`参照（任意） | アンカー位置。`Attachment0`があればその軸系を`Axis`の基準にする |
| `Axis` | `Vector3` | 移動軸。Cube0ローカル（既定 (0,1,0)）。変更時はjointを再生成する |
| `LimitsEnabled` | `bool` | 可動範囲制限の有効/無効（既定false） |
| `LowerLimit`/`UpperLimit` | `float` | 可動範囲(stud)。Cube0アンカーからCube1アンカーまでの軸方向距離に対する範囲 |
| `Enabled` | `bool` | 制約の有効/無効 |

## 挙動
- Cube1側アンカーは軸直交方向のずれを除いて軸線上へ置く（生成時の初期スナップを避ける）。軸方向のずれは初期translationとして残る。
- Box3Dのprismatic jointはjoint frame AのローカルX軸方向に動くため、`Axis`をX軸へ回す回転でframeを作る。

## 描画
「物理デバッグ表示」ON時のみ`Renderer::renderPhysicsDebug`が描画する: 2アンカー間の線、軸方向の矢印、`LimitsEnabled`時は可動範囲の線分と両端のクロス。

## 依存関係
`BaseCube`, `Attachment`, `Workspace`, `Physics`（`createPrismatic`）, `Box3DPhysicsBackend`, `Renderer`（friend）
