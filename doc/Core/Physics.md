# Physics

`include/Core/Physics.hpp`

Box3Dを使った剛体物理シミュレーション。Cubeへのbody割り当て、固定ステップ、raycast、constraintを担当する。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_backend` | `unique_ptr<IPhysicsBackend>` | 現行のBox3D backend |

## メソッド

| メソッド | 説明 |
|---|---|
| `init()` | Box3D backendを初期化 |
| `createActor(cube)` | Cubeに対応するBox3D body/shapeを生成 |
| `recreateActor(cube)` | サイズ変更時にアクターを再構築 |
| `removeCube(cube)` | アクターをシーンから削除・メモリ解放 |
| `update(workspace, dt)` | 物理ステップを進め、全キューブの `syncPhysics()` を呼ぶ |
| `raycast(origin, direction, maxDistance, hitResult, ignoreActor)` | 光線と物理シーンの交差判定 |
| `getBodyMass(cube)` | 現在のnative body massをread-onlyで取得。不正body/massはpath付きで報告 |
| `getAngularVelocity(cube)` | native bodyの角速度をread-onlyで取得。body未接続時はゼロ |
| `consumeContactImpact(cube)` | 前回physics update以降のCube単位最大contact impactを返して消費。Box3Dはnormal impulseを優先し、無い場合はnormal approach speedをstud/s相当へ換算 |

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
  2. Box3Dの固定ステップを進める
  3. Gyro等のconstraint制御を適用する
  4. 各 BaseCube::syncPhysics() で位置・回転を取り込む

Weld assembly の body 原点と member のワールド CFrame は別概念である。外部 API は member ワールド姿勢を基準にし、body 原点への変換は backend 内部だけで行う。Topology/形状変更は固定ステップ前にまとめ、rebuild 前の姿勢・速度・sleep 状態を保持する。NoCollision は指定 pair のフィルタだけを変更する。
```

Box3D worldは内蔵スケジューラを使用する。worker数は論理プロセッサ数の半分を目安に、呼び出し元threadを含む2〜4へ制限する（単一論理プロセッサでは1）。Box3Dの推奨どおり効率コアやSMTを無制限に使用せず、小規模sceneのtask scheduling overheadを抑える。

`syncAllCubes()`はBodyEntryに保持した共有body状態を使い、Cubeごとの全Body再検索を行わない。単独Anchored Cubeは最後にnative bodyへ送ったworld CFrameと現在値が同一ならBox3Dへのtarget transform更新を省略する。親Spatialの移動等でworld CFrameが変わった場合は次の同期で更新する。Weld共有bodyとdynamic bodyは従来どおりnative bodyを正としてmember姿勢を反映する。

Gyroは単一Partのworld角度をX/Y/Z軸ごとに独立制御する。各軸の目標角、最大トルク、最大角速度は
Gyro Instanceが保持し、Box3D backendは有効な軸だけへPD制御トルクを加える。

## 依存関係

- Box3D
- `BaseCube`, `Vector3`, `Material`, `PhysicsConstraint`

## 使われる場所

- `main.cpp` でフレームごとに `update()` を呼ぶ
- `User::processInput()` がジャンプ判定などに `raycast()` を使用
- `Workspace::setPhysicsEngine()` で接続される
