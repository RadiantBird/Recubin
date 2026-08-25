# Humanoid

`include/Instances/Humanoid.hpp`

キャラクターコントローラー。StarterCharacter内のテンプレート、またはそのclone後のModelから `Root`/`Torso`/`Head`/`LeftArm`/`RightArm`/`LeftLeg`/`RightLeg` という名前の兄弟 `Cube`/`Sphere` を探して参照し、移動・ジャンプ・接地判定・歩行アニメーション・一人称時の身体非表示・ヘルス管理・死亡演出（ラグドール）・キーフレームアニメーション再生を一括して行う（spec.md「キャラクター」節）。GLFWwindow/SystemStateには依存せず、入力はUser側がベクトル/boolへ変換して渡す。

身体パーツの所有者は親Modelの `children` であり、Humanoidはprivateな `weak_ptr` だけを保持する。C++側は各パーツgetterが返す一時的な `shared_ptr` を処理中だけ保持するため、親Modelの破棄後にHumanoidだけが残っても身体パーツの寿命は延長されない。この参照はLuau/YAMLプロパティやシリアライズ形式には公開されない。

## 継承
`Instance` → `Humanoid`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `WalkSpeed` | `float` | 歩行速度（[0,100]にクランプ、旧CharacterSetting.moveSpeedの統合先） |
| `JumpPower` | `float` | ジャンプ初速（[0,100]にクランプ） |
| `Health` / `MaxHealth` | `float` | 現在/最大ヘルス |
| `RespawnTime` | `float` | 死亡後の再生成までの秒数 |
| `Died` | `shared_ptr<RCBNScriptSignal>` | Health<=0で1度だけ発火 |
| `KeyframeReached` | `shared_ptr<RCBNScriptSignal>` | 再生位置が既存キーフレームの時刻を通過した瞬間に発火。引数は`(partName: string, time: number)` |
| `m_root`/`m_torso`/`m_head`/`m_leftArm`/`m_rightArm`/`m_leftLeg`/`m_rightLeg` | `weak_ptr<BaseCube>` | `resolveParts()` で解決されるprivateな非所有の兄弟パーツ参照 |
| `m_dead` | `bool` | 死亡フラグ |
| `walkCycle` | `float` | 歩行アニメーションの位相（0..1） |
| `isGrounded` | `bool` | 接地判定結果 |
| `isFirstPerson` / `bodyColorsSaved` / `saved*Color` | - | 一人称時の身体非表示・色の退避用 |
| `m_walkAnimation`/`m_jumpAnimation`/`m_equipAnimation` | `weak_ptr<Animation>` | Scene Tree上のAnimationへの非所有参照 |
| `m_currentAnim`/`m_animTime`/`m_animPlaying` | - | カスタムAnimationの再生状態 |

`JumpHeight`（get/set）はJumpPowerとの相互変換プロパティ（`Units.hpp`の重力定数を使用、`h = JumpPower²/(2g)`）。

## メソッド

| メソッド | 説明 |
|---|---|
| `resolveParts(characterModel)` | 親Modelの子から名前でパーツ解決。Rootの角度(X/Z軸)をロックし転倒防止 |
| `resolveAnimationReferences(characterModel)` | YAMLの保存パスをTree構築後にAnimation参照へ解決。失敗時も保存パスを保持 |
| `set/getWalkAnimation`、`set/getJumpAnimation`、`set/getEquipAnimation` | Animation参照を明示的に設定・取得 |
| `getRootPart()`/`getTorsoPart()`/`getHeadPart()`/左右の腕・脚getter | C++処理向けにweak参照を一時的な`shared_ptr`へ昇格。期限切れ時は`nullptr` |
| `setRootPart(root)` | ネットワーク予測用Rootの非所有参照を設定し、通常Rootと同じ角度ロックを適用。Luau/YAMLには非公開 |
| `move(...)` | WASD相当の入力から移動・回転・壁ずり・歩行アニメ・接地判定・身体配置を実行 |
| `moveToward(target, physics, arrivalRadius)` | パス追従用の1フレーム移動（`move()`のロジックを流用） |
| `jump()` | 接地中のみJumpPowerで上方向速度をセット |
| `setHealth(v)`/`takeDamage(n)` | クランプしつつ設定。0以下遷移でDied発火 |
| `enterRagdoll(physics)` | 全身パーツを動的アクター化しランダム速度で吹き飛ばす |
| `playAnimation`/`pauseAnimation`/`stopAnimation`/`setAnimationSpeed` | Animation再生制御 |
| `updateAnimation(dt)` | AnimationClip（内蔵または.rcanim）を評価し、RigのJoint/Pivotバインドオフセットと合成してパーツCFrameを更新（Rootは物理駆動のため対象外） |
| `updateFirstPersonState(wantsFirstPerson)` | 一人称/三人称切替時に身体色を透明化/復元 |
| `getRootWorldPosition()`/`getHeadWorldPosition()` | ワールド座標取得 |
| `applyBodyAnimation(leftArmRaised, rightArmRaised)` | Pose計算結果をリグ定義に基づき各パーツへ適用 |

## フロー — move()の処理順

```
move(flatForward, flatRight, isPressingMove, targetMoveDir, ctrlLockEnabled, physics, ..., smoothing)
  ├─ currentMoveDir をtargetMoveDirへ補間(User.CharacterSmoothing、既定0.15)
  ├─ 向き決定: CtrlLock中はカメラ正面 / 移動中は移動方向 へSlerp
  ├─ 壁ずり: 進行方向にレイキャストし、法線成分を速度から除去
  ├─ PhysXアクターへ水平速度を適用（Y速度は保持）
  ├─ walkCycle更新（押下中は加算、離した後は0.5basisで戻す）
  ├─ 接地判定: Root下方向へレイキャスト
  └─ applyBodyAnimation() でボディパーツを再配置
```

## フロー — ジャンプ/死亡演出

```
jump(): isGrounded && Root->actor が真の場合のみ Y速度=JumpPower をセット

enterRagdoll(physics):
  stopAnimation()
  Root: CanCollide=false, recreateActor() （散乱物に干渉させない不可視物理体）
  Torso/Head/LeftArm/RightArm/LeftLeg/RightLeg 各パーツ:
    CanCollide=true, Anchored=false, LockFlags解除
    recreateActor() → ランダムな水平速度・上方向速度・角速度を設定
```

## 依存関係

- `BaseCube`, `Cube`, `Sphere`, `Animation`, `Physics`, `Spatial`
- `RCBNScriptSignal`（Died）
- `PropertyRegistry`（WalkSpeed/JumpPower/JumpHeight/MaxHealth/RespawnTime/Health/Diedを一括登録）
- `Math/Units.hpp`（重力定数によるJumpHeight換算）
- `Core/AnimationClip`（内蔵R6 Walk／プロジェクト`.rcanim`共通ランタイム表現）

## Animation参照とフォールバック

`WalkAnimation`、`JumpAnimation`、`EquipAnimation`はProperties、YAML、Luauから参照できる。
StarterCharacterをPlayerCharacterへcloneすると、3参照ともclone先のAnimationへ再接続され、
テンプレート側を参照し続けない。現在データ化済みの既定再生はWalkである。

Walk参照先の`.rcanim`が欠損・破損しても参照パスとAnimation Instanceは保持し、Sceneを変更せず
実行中だけ内蔵Walkを評価する。参照自体がない旧Characterでは、StarterCharacterを変更せず、
PlayerCharacter側だけに可視な`Source=BuiltIn`のR6Walk Animationを追加する。

## 継承クラス

なし

ネットワーク接続中の離席要求はClientからHostへ入力シーケンスとして送信され、Host側の代理Humanoidが`standUp()`を実行してSeatWeldとOccupantを権威的に解除する。
