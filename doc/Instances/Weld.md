# Weld

`include/Instances/Weld.hpp`

2つの`BaseCube`を`PxRigidDynamic`のコンパウンド（剛体結合）で固定する物理制約インスタンス。Motorと同様に`Cube0`/`Cube1`が揃うと自動でWorkspaceに登録される。`collectAssembly()`により、Weldで連結されたアセンブリ全体をMotor境界を越えずにBFS収集できる（ラグドールやアセンブリ操作で使用想定）。

## 継承
`Instance` → `Weld`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_cube0`/`m_cube1` | `weak_ptr<BaseCube>` | 接続対象（弱参照） |
| `m_cube0Name`/`m_cube1Name` | `string` | 名前ベースの遅延解決用 |
| `m_compound` | `PxRigidDynamic*` | 結合された剛体 |
| `m_lastWorkspace` | `Workspace*` | 登録済みWorkspaceのキャッシュ |

## メソッド

| メソッド | 説明 |
|---|---|
| `setCubes`/`setCube0`/`setCube1` | Motorと同様のセット＋`registerIfReady()` |
| `registerIfReady()`（private） | 両Cube解決済みならWorkspaceへ登録 |
| `collectAssembly(start, root)`（static） | `start`から到達可能な、Weldで繋がった`BaseCube`群をBFS収集。Motorで接続された辺は境界として越えない |
| `onAncestorChanged()` | Workspace配下ならconstraint登録、外れたら解除 |
| `setProperty(name, value)` | `Cube0`/`Cube1`は、Workspace相対で見つからなければ最上位祖先(System等)相対でも解決（StarterCharacter対応） |
| `clone()`/`remapClonedInstances(map)` | サブツリー複製時のCube参照張り替え |

## フロー — collectAssembly() のBFS

```
root配下から全Weld/Motorを収集
  → Motorで繋がるCubeペアを「越えてはいけない境界」として登録
  → startからBFS開始
      Weldで繋がる隣接Cubeを訪問（Motor境界のペアはスキップ）
  → 訪問済み全BaseCubeのvectorを返す
```

## 依存関係

- `BaseCube`, `Motor`（境界判定のため参照）, `Workspace`, `Physics`
- PhysX (`PxRigidDynamic`)

## 継承クラス

なし（Motor/Rope/Rodは兄弟クラス）
