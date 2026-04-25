# Physics

`include/Core/Physics.hpp`

PhysX を使った剛体物理シミュレーション。キューブへのアクター割り当て・シミュレーションステップ・レイキャストを担当。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `foundation` | `PxFoundation*` | PhysX 基盤 |
| `physics` | `PxPhysics*` | PhysX メインインタフェース |
| `scene` | `PxScene*` | 物理シーン |
| `materialCache` | `unordered_map<MaterialType, PxMaterial*>` | マテリアルキャッシュ |
| `allocator` | `PxDefaultAllocator` | PhysX アロケータ |
| `errorCallback` | `PxDefaultErrorCallback` | PhysX エラーコールバック |
| `cubes` | `vector<BaseCube*>` | 管理中のキューブリスト |

## メソッド

| メソッド | 説明 |
|---|---|
| `init()` | PhysX を初期化 |
| `createActor(cube)` | キューブに対応する PhysX 形状を生成してシーンへ追加 |
| `recreateActor(cube)` | サイズ変更時にアクターを再構築 |
| `removeCube(cube)` | アクターをシーンから削除・メモリ解放 |
| `update(workspace, dt)` | 物理ステップを進め、全キューブの `syncPhysics()` を呼ぶ |
| `raycast(origin, direction, maxDistance, hitResult, ignoreActor)` | 光線と物理シーンの交差判定 |

## RaycastHit 構造体

```cpp
struct RaycastHit {
    bool hit;
    float distance;
    Vector3 position;
    Instance* instance;
};
```

## フレームループ内での動作

```
Physics::update(workspace, dt)
  1. pendingInstances を走査して createActor()
  2. scene->simulate(dt)
  3. scene->fetchResults()
  4. 各 BaseCube::syncPhysics() で位置・回転を取り込む
```

## 依存関係

- PhysX SDK
- `BaseCube`, `Vector3`, `Material`

## 使われる場所

- `main.cpp` でフレームごとに `update()` を呼ぶ
- `User::processInput()` がジャンプ判定などに `raycast()` を使用
- `Workspace::setPhysicsEngine()` で接続される
