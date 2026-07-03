# Rope

`include/Instances/Rope.hpp`

2つの`BaseCube`を`PxDistanceJoint`（バネ付き距離ジョイント）で接続する物理制約インスタンス。`Stiffness`/`Damping`でバネ挙動を制御し、`MaxDistance`（0なら生成時の距離を自動使用）で最大長を制限する。`Color`/`LineWidth`は描画用（Rendererがfriend指定で参照）。

## 継承
`Instance` → `Rope`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_cube0`/`m_cube1` | `weak_ptr<BaseCube>` | 接続対象（弱参照） |
| `m_cube0Name`/`m_cube1Name` | `string` | 名前ベースの遅延解決用 |
| `m_joint` | `PxDistanceJoint*` | PhysXジョイント実体 |
| `m_lastWorkspace` | `Workspace*` | 登録済みWorkspaceのキャッシュ |
| `MaxDistance` | `float` | 最大距離。0=生成時の距離を自動使用 |
| `Stiffness` | `float` | バネ剛性（既定100.0） |
| `Damping` | `float` | バネ減衰（既定10.0） |
| `Color` | `Color4` | 描画色（既定 水色系） |
| `LineWidth` | `float` | 描画線幅（既定2.5） |

## メソッド

| メソッド | 説明 |
|---|---|
| `setCubes`/`setCube0`/`setCube1` | Motor/Weldと同様のセット＋`registerIfReady()` |
| `registerIfReady()`（private） | 両Cube解決済みならWorkspaceへ登録 |
| `setMaxDistance(v)` | 値更新＋既存ジョイントの`setMaxDistance`に反映 |
| `setStiffness(v)` | 値更新＋`setStiffness`と`eSPRING_ENABLED`フラグ(v>0で有効)を反映 |
| `setDamping(v)` | 値更新＋既存ジョイントの`setDamping`に反映 |
| `onAncestorChanged()` | Workspace配下ならconstraint登録、外れたら解除 |
| `setProperty(name, value)` | `Cube0`/`Cube1`/`MaxDistance`/`Stiffness`/`Damping`/`Color`/`LineWidth`を処理 |
| `clone()`/`remapClonedInstances(map)` | サブツリー複製時のCube参照張り替え |

## 依存関係

- `BaseCube`, `Workspace`, `Physics`（PxDistanceJoint）, `Color4`
- `Renderer`（friend、ロープ描画のためm_joint等を参照）
- PhysX (`PxDistanceJoint`)

## 継承クラス

なし（Motor/Weld/Rodは兄弟クラス）
