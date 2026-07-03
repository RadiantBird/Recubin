# Motor

`include/Instances/Motor.hpp`

2つの`BaseCube`を`PxRevoluteJoint`（回転ジョイント）で接続し、指定軸まわりに角速度を与えて回転駆動させる物理制約インスタンス。ツリー構造のどこにあっても有効で、`Cube0`/`Cube1`が両方解決されると自動的にWorkspaceへ登録される（spec.md「物理制約」節）。Motor/Weld/Rope/Rodは共通の基底クラスを持たず、各クラスが個別に`Instance`を継承し、同一の`m_cube0`/`m_cube1`/`registerIfReady()`パターンを重複実装している。

## 継承
`Instance` → `Motor`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_cube0`/`m_cube1` | `weak_ptr<BaseCube>` | 接続対象（弱参照） |
| `m_cube0Name`/`m_cube1Name` | `string` | 名前ベースの遅延解決用（YAML保存対象） |
| `m_joint` | `PxRevoluteJoint*` | PhysXジョイント実体 |
| `m_lastWorkspace` | `Workspace*` | 登録済みWorkspaceのキャッシュ（破棄時の制約解除に使用） |
| `Axis` | `Vector3` | 回転軸（ワールド方向、既定`{1,0,0}`） |
| `DriveVelocity` | `float` | 目標角速度(rad/s) |
| `MaxForce` | `float` | 駆動力の上限 |

## メソッド

| メソッド | 説明 |
|---|---|
| `setCubes(cube0, cube1)` | 両Cubeを直接セット |
| `setCube0`/`setCube1` | Cubeをセットし名前を保存、`registerIfReady()`呼び出し |
| `registerIfReady()`（private） | 両Cubeが解決済みならWorkspaceへ`registerConstraint()` |
| `setDriveVelocity(v)`/`setMaxForce(v)` | 値更新＋既存ジョイントへ即時反映 |
| `onAncestorChanged()` | Workspace配下ならconstraint登録、外れたらPhysicsから解除 |
| `setProperty(name, value)` | `Cube0`/`Cube1`(名前解決)/`Axis`/`DriveVelocity`/`MaxForce`を処理後`registerIfReady()` |
| `clone()`/`remapClonedInstances(map)` | サブツリー複製時にCube参照を複製先へ張り替え |

## フロー — 制約自動初期化

```
Cube0/Cube1 プロパティ設定 (setProperty または setCube0/setCube1)
  → m_cube0Name/m_cube1Name に名前を保存
  → Workspace(またはSystem等の最上位祖先)からgetChildByPathで解決を試行
  → registerIfReady():
      両方 lock() 可能なら ws->registerConstraint(this) → Physics側でPxRevoluteJoint生成
      片方のみなら何もしない（次のプロパティ設定時や onAncestorChanged で再試行）
```

## 依存関係

- `BaseCube`, `Workspace`, `Physics`（PxRevoluteJoint生成・破棄）
- PhysX (`PxPhysicsAPI.h`)

## 継承クラス

なし（Weld/Rope/Rodは兄弟クラスであり派生関係はない）
