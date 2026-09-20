# Humanoid

`include/Instances/Humanoid.hpp`

キャラクターコントローラー。StarterCharacter内のテンプレート、またはそのclone後のModelから `Root`/`Torso`/`Head`/`LeftArm`/`RightArm`/`LeftLeg`/`RightLeg` という名前の兄弟 `Cube`/`Sphere` を探して参照し、移動・ジャンプ・接地判定・歩行アニメーション・一人称時の身体非表示・ヘルス管理・死亡演出（ラグドール）・キーフレームアニメーション再生を行う（spec.md「キャラクター」節）。接地・GroundHeight hover・Truss重力は `updatePhysicsState()` でControlModeから独立して更新する。GLFWwindow/SystemStateには依存せず、入力はUser側がベクトル/boolへ変換して渡す。

身体パーツの所有者は親Modelの `children` であり、Humanoidはprivateな `weak_ptr` だけを保持する。C++側は各パーツgetterが返す一時的な `shared_ptr` を処理中だけ保持するため、親Modelの破棄後にHumanoidだけが残っても身体パーツの寿命は延長されない。この参照はLuau/YAMLプロパティやシリアライズ形式には公開されない。

## 継承
Root の移動・回転は Root member のワールド CFrame を基準に行う。身体 Animation は local pose を更新し、Parent や Weld 用の座標補正を行わない。

接地判定とGroundHeight hoverは同じ1回の下向きshape cast結果を使う。目標distanceは`HipHeight`で、各dynamic R6
bodyの予約child `CharacterHoverForce`へ`body mass × upward acceleration`を設定する。HipHeightは既定値3 studを持つ
保存プロパティであり、Root中心からsupport surfaceまでのhover目標値と
着地捕捉に使い、接地状態は捕捉後の微小なfloor distance揺れでは反転させず、床が消えるかRootが上昇した
ときに解除する。jump開始時、死亡、
着席、無効状態、Ragdoll中では全hover Forceをzero/disabledにする。jump上昇中は再開しない。下降中のcapture範囲は
3 studを下限とし、現在の下降速度・最大上向き加速度・重力から求めた制動距離に1 physics step分の安全余裕を加えた値まで広げる。
この範囲を同じshape cast検出距離へ反映して、高速のTruss jumpや落下でもRootがHipHeightを通過する前に制動を開始する。
PD係数は`CharacterRig::groundHeightSettings()`へ集約し、Workspaceの現在重力を相殺する。

`Normal`/`ClimbingUp`/`ClimbingDown`/`Ragdoll`/`Recovering`状態を明示的に持つ。Truss接触中にW入力があると
`ClimbingUp`、S入力があると`ClimbingDown`へ遷移し、垂直入力が無い場合は`Normal`へ戻る。昇降中はCharacter Modelの
全dynamic R6 bodyの重力を無効化し、各bodyの`CharacterClimbForce`で水平方向のストレイフ速度と`ClimbSpeed`の昇降速度を
MaintainVelocityとして適用する。Trussから離れたらForceを無効化し、全bodyの重力を復帰する。
`ClimbingUp`/`ClimbingDown`中のJumpは接地判定を待たずに実行でき、`Normal`へ戻ってTruss用Forceと重力制御を解除する。
`ClimbingDown`中はRoot footprintの下方向box castで上向きのsupport面を探し、`HipHeight + 0.2 stud`まで近づくと
自動で`Normal`へ戻る。このとき全bodyの負のY速度だけを0へ戻る。Trussが地面まで生えていても、RootがTrussのAABBを
抜けるまで再度Truss制御へ入らない。

Box3Dのhit eventで得た接触点の`totalNormalImpulse`を優先し、
取得できない場合は接近速度を接触法線方向のimpactとして扱う。Characterの全bodyについてphysics tick内の最大値だけを
評価し、`ImpactRagdollThreshold`以上でRagdollへ遷移する。空中状態からlanding captureへ入る時は、全bodyの下向き成分だけを質量加重した
垂直運動エネルギーから通常の3-stud captureで吸収できる分を差し引いた残余を同じstud/s相当へ換算して判定する。
既定値は45 stud/s相当で、通常の短いjump着地では発動しにくく、強い床・壁・物体衝突と高速Truss jump/高所落下を対象にする。

`Instance` → `Humanoid`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `WalkSpeed` | `float` | 歩行速度（[0,100]にクランプ、旧CharacterSetting.moveSpeedの統合先） |
| `JumpPower` | `float` | ジャンプ初速（[0,100]にクランプ） |
| `HipHeight` | `float` | Root中心から真下のsupport surfaceまでの保存された目標距離（既定3 stud） |
| `ImpactRagdollThreshold` | `float` | 接触impactまたは高速着地の残余エネルギー換算値がこの値以上でRagdollへ遷移（既定45） |
| `RagdollRecoverySpeed` | `float` | 復帰判定に使うRoot線速度上限（既定2.5 stud/s） |
| `RagdollRecoveryDelay` | `float` | 低速・接地状態を継続する時間（既定1秒） |
| `Health` / `MaxHealth` | `float` | 現在/最大ヘルス |
| `RespawnTime` | `float` | 死亡後の再生成までの秒数 |
| `Died` | `shared_ptr<RCBNScriptSignal>` | Health<=0で1度だけ発火 |
| `KeyframeReached` | `shared_ptr<RCBNScriptSignal>` | 再生位置が既存キーフレームの時刻を通過した瞬間に発火。引数は`(partName: string, time: number)` |
| `m_root`/`m_torso`/`m_head`/`m_leftArm`/`m_rightArm`/`m_leftLeg`/`m_rightLeg` | `weak_ptr<BaseCube>` | `resolveParts()` で解決されるprivateな非所有の兄弟パーツ参照 |
| `m_dead` | `bool` | 死亡フラグ |
| `m_state` | `Normal/ClimbingUp/ClimbingDown/Ragdoll/Recovering` | 通常姿勢制御、Truss昇降、物理Ragdoll、物理upright復帰の状態 |
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
| `move(...)` | WASD相当の入力から移動、RootのYawForceがあればそのY角速度、なければGyroのY方位、歩行アニメ、身体配置を更新。無入力時は現在の方位を維持 |
| `updatePhysicsState(physics)` | ControlModeに関係なく接地raycast、GroundHeight hover、Truss中の重力設定を更新 |
| `stopCharacterMotion(physics)` | Character操作からFree/Programへ移行する際、全身の水平・角速度を停止し、Character専用YawForceを無効化して垂直速度を保持 |
| `moveToward(target, physics, arrivalRadius)` | パス追従用の1フレーム移動（`move()`のロジックを流用） |
| `jump()` | 通常時は接地中、Climbing中は接地判定なしでJumpPowerを適用。Climbing中のJumpは`Normal`へ戻してTrussから脱出 |
| `setHealth(v)`/`takeDamage(n)` | クランプしつつ設定。0以下遷移でDied発火 |
| `enterRagdoll(physics)` | Motor6Dを無効化し、既存R6 BallSocketを有効化してcollision/Root lock/hover/yawを切り替える |
| `recoverFromRagdoll(physics)` | 条件成立後に`Recovering`へ遷移する。BallSocketを先に無効化し、Motor6D bind poseとRootGyroで物理的にuprightへ戻す |
| `playAnimation`/`pauseAnimation`/`stopAnimation`/`setAnimationSpeed` | Animation再生制御 |
| `updateAnimation(dt)` | AnimationClip（内蔵または.rcanim）を評価し、RigのJoint/Pivotバインドオフセットと合成してパーツCFrameを更新（Rootは物理駆動のため対象外） |
| `updateFirstPersonState(wantsFirstPerson)` | 一人称/三人称切替時に身体色を透明化/復元 |
| `getRootWorldPosition()`/`getHeadWorldPosition()` | ワールド座標取得 |
| `applyBodyAnimation(leftArmRaised, rightArmRaised)` | Pose計算結果をリグ定義に基づき各パーツへ適用 |

## フロー — move()の処理順

```
move(flatForward, flatRight, isPressingMove, targetMoveDir, ctrlLockEnabled, physics, ..., smoothing)
  ├─ currentMoveDir をtargetMoveDirへ補間(User.CharacterSmoothing、既定0.15)
  ├─ flatForwardをCharacterSmoothingでdt補間したheadingを更新
  ├─ 向き決定: CtrlLock中は平滑化済みカメラ正面 / 移動中は平滑化済み移動方向
  ├─ 壁ずり: 進行方向にレイキャストし、法線成分を速度から除去
  ├─ Box3D bodyへ水平速度を適用（Y速度は保持）
  ├─ walkCycle更新（押下中は加算、離した後は0.5basisで戻す）
  ├─ 接地判定: Root下方向へレイキャスト
  └─ applyBodyAnimation() でボディパーツを再配置
```

## フロー — ジャンプ/死亡演出

```
jump(): NormalではisGroundedまたは水中、Climbingでは接地判定なしで全bodyのY速度=JumpPowerをセット。Climbing時はNormalへ戻る

enterRagdoll(physics):
  state=Ragdoll, 再生中Animationの状態と時刻を保持したまま評価を一時停止し、hover/yaw/Gyro/Root lockを無効化
  Motor6Dを無効化し、同名Motor6DのC0/C1 bind anchorを使うBallSocketを有効化
  R6 bodyのcollisionを一時的に有効化する（通常Characterの内部self-collisionは抑制するが、
  Ragdoll中にBallSocketで管理されるbody同士は異なるBallSocket chain間でもcollisionを許可し、
  明示的なNoCollisionを優先する）
  Neck/Shoulder/HipのBallSocketはlocal joint frame基準のAngularX/Y/Z制限を使うため、Neckの360度回転や
  肩・股関節の裏返りを抑止する。復帰時はBallSocketを先に無効化してからMotor6Dを再有効化する

recoverFromRagdoll(physics):
  Root線速度がRagdollRecoverySpeed以下、角速度が2.0 rad/s以下、RecoveryDelay成立を確認してstate=Recovering。
  通常はbodyの接地も確認し、support scanが取れない場合は追加0.75秒後に接地なしでもRecoveringへ進む
  BallSocketを無効化してMotor6DのTransformをbind poseへ戻し、RootGyroのX/Y/Zで物理的にuprightへ戻す
  uprightError <= 15度、Pitch/Roll誤差 <= 15度、Root角速度 <= 1.5 rad/sで0.1秒安定したらgyro-successとして最終化する
  Recovering専用support scanはRootのX/Z footprintを薄いboxとしてRootの想定足元より少し上から下方へshape castする（Rootが沈んだ場合も床を拾える）。Character自身を除外し、
  normal.y >= 0.5の上向き面だけを採用する。複数候補では最も高いsupport面を使う。supportはY補正に利用するが、scanの一時的な失敗を
  復帰不能の理由にしない
  現在のX/ZとRecovering開始時の有効Yawを維持し、Pitch/Rollだけを除去する。Yは`max(currentRootY, supportY + HipHeight)`で計算し、
  めり込み回避に必要な最小上方向補正だけを加えたCFrameを一度だけ適用する
  Gyroでuprightへ到達できない場合も、Recovering開始から0.5秒後にRoot線速度 <= 4.0、角速度 <= 2.5 rad/sなら
  speed-fallbackとして同じ最終CFrame正規化を一度だけ行う。さらに1.25秒経過後はsupportや速度に関係なくtimeout-fallbackで最終化する。
  supportが無い場合のYは現在Root Yを維持する
  直後にRoot角速度、Root lock、通常collision、Gyro/YawForce、hover、movement、jumpを順に復元してNormalへ戻す
  Recovering中はmovement、jump、hover、通常アニメーションを無効にし、BallSocketとMotor6Dを同時に有効化しない
  Normal復帰後はRagdoll前に再生中だったAnimationを同じ再生時刻から再開する
```

## 依存関係

- `BaseCube`, `Cube`, `Sphere`, `Animation`, `Physics`, `Spatial`
- `RCBNScriptSignal`（Died）
- `PropertyRegistry`（WalkSpeed/JumpPower/ClimbSpeed/HipHeight/JumpHeight/MaxHealth/RespawnTime/Health/Diedを一括登録）
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
