# Rod

`include/Instances/Rod.hpp`

2つの`BaseCube`を`PxDistanceJoint`で接続する剛体棒（伸縮しない距離拘束）の物理制約インスタンス。Ropeと異なりバネパラメータ（Stiffness/Damping/MaxDistance）を持たず、常に生成時の距離を固定長として扱う。`Color`/`LineWidth`は描画用（Rendererがfriend指定で参照）。

## 継承
`Instance` → `Rod`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_cube0`/`m_cube1` | `weak_ptr<BaseCube>` | 接続対象（弱参照） |
| `m_cube0Name`/`m_cube1Name` | `string` | 名前ベースの遅延解決用 |
| `m_joint` | `PxDistanceJoint*` | PhysXジョイント実体 |
| `m_lastWorkspace` | `Workspace*` | 登録済みWorkspaceのキャッシュ |
| `Color` | `Color4` | 描画色（既定 橙系） |
| `LineWidth` | `float` | 描画線幅（既定2.5） |

## メソッド

| メソッド | 説明 |
|---|---|
| `setCubes`/`setCube0`/`setCube1` | Motor/Weld/Ropeと同様のセット＋`registerIfReady()` |
| `registerIfReady()`（private） | 両Cube解決済みならWorkspaceへ登録 |
| `onAncestorChanged()` | Workspace配下ならconstraint登録、外れたら解除 |
| `setProperty(name, value)` | `Cube0`/`Cube1`/`Color`/`LineWidth`を処理 |
| `clone()`/`remapClonedInstances(map)` | サブツリー複製時のCube参照張り替え |

## 依存関係

- `BaseCube`, `Workspace`, `Physics`（PxDistanceJoint）, `Color4`
- `Renderer`（friend、棒描画のためm_joint等を参照）
- PhysX (`PxDistanceJoint`)

## 継承クラス

なし（Motor/Weld/Ropeは兄弟クラス）
