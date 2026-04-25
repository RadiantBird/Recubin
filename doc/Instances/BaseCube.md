# BaseCube

`include/Instances/BaseCube.hpp`

物理シミュレーション対応の 3D キューブ基底クラス。PhysX アクターを保有し、ワールドに追加されると自動的に物理エンジンへ登録される。

## 継承

`Instance` → `Spatial` → `BaseCube`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Anchored` | `bool` | true = 静的オブジェクト（重力・力の影響なし） |
| `CanCollide` | `bool` | 衝突判定の有無 |
| `Color` | `Color4` | オブジェクト色 |
| `material` | `Material` | 物理マテリアル（摩擦・反発） |
| `lastWorkspace` | `Workspace*` | 登録済み Workspace のキャッシュ |
| `actor` | `physx::PxRigidActor*` | PhysX 剛体アクター |
| `LockFlags` | `PxRigidDynamicLockFlags` | 軸ロック設定 |

## メソッド

| メソッド | 説明 |
|---|---|
| `syncPhysics()` | PhysX から位置・回転を読み取り CFrame に反映 |
| `teleportTo(pos)` | 物理をバイパスして位置を直接設定 |
| `setSize(newSize)` | サイズ変更 → PhysX アクターを再構築 |
| `onAncestorChanged()` | Workspace への追加/削除時に自動で物理登録/解除 |
| `setProperty(name, value)` | YAML デシリアライズ用 |

## 物理登録フロー

```
setParent(workspace)
  → onAncestorChanged()
    → findFirstAncestorWorkspace()
    → Workspace::registerCube(this)
      → pendingInstances に追加
        → Physics::createActor() が次フレームで実行
```

## 依存関係

- `Spatial`, `Workspace`, `Material`, `Color4`
- PhysX SDK

## 継承クラス

- `Cube`（描画機能を追加）
