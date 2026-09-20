# BaseCube

`include/Instances/BaseCube.hpp`

Box3D物理シミュレーション対応の3D基底クラス。Workspaceへ追加されると自動的に物理エンジンへ登録される。

## 継承

`Instance` → `Spatial` → `BaseCube`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Anchored` | `bool` | true = 静的オブジェクト（重力・力の影響なし） |
| `CanCollide` | `bool` | 物理的な反発・拘束の有無 |
| `CanTouch` | `bool` | overlap接触イベントへの参加。既定値は`true` |
| `CastShadow` | `bool` | false の場合は常に影を生成しない |
| `ShadowMode` | `ShadowMode` | `Always`（透明度を無視）、`Never`（影なし）、`Normal`（`Color.a > 0.001`）。既定値は `Normal`。MeshCube の fallback geometry は `Normal` でも影を生成する |
| `Color` | `Color4` | オブジェクト色 |
| `material` | `Material` | 物理マテリアル（摩擦・反発） |
| `lastWorkspace` | `Workspace*` | 登録済み Workspace のキャッシュ |
| `LockFlags` | `PhysicsLockFlags` | 軸ロック設定 |

`Touched`は物理衝突ではなく、形状overlapの開始時に発火する。`TouchEnded`はoverlap終了時に発火する。物理的な衝突を無効にした`CanCollide=false`のパーツでもイベントは発火する。ペアの両方が`CanTouch=true`の場合だけ通知され、`CanTouch=false`はイベントだけを抑止して物理衝突設定には影響しない。NoCollisionやCharacter自己衝突規則も物理応答にだけ適用される。

## メソッド

| メソッド | 説明 |
|---|---|
| `syncPhysics()` | Box3D から位置・回転を読み取り CFrame に反映 |
| `teleportTo(pos)` | member のローカル位置を物理へ反映する移動 |
| `setSize(newSize)` | サイズ変更 → Box3D body/shape を再構築 |
| `onAncestorChanged()` | Workspace への追加/削除時に自動で物理登録/解除 |
| `setProperty(name, value)` | YAML デシリアライズ用 |

## 物理登録フロー

Weld で接続された BaseCube は一つの assembly/body として扱われる。member の姿勢は Spatial のワールド CFrame と body 原点の間で変換され、Parent 変更だけでは子孫を追従させない。

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
- Box3D SDK

## 継承クラス

- `Cube`（描画機能を追加）
