# Seat

`include/Instances/Seat.hpp`

座席。RobloxのVehicleSeat相当。Seatは自身にTouchセンサーを持ち(`setNativeTouchObserved(true)`)、`Humanoid`の足(`LeftLeg`/`RightLeg`)が触れると`onNativeTouched`から`sit`を呼び、未占有であれば自動的に着席させる(`Humanoid::sitOn`): Rootを座面(Seat中心+Size.y/2)にRoot底面が乗る高さへスナップし、ハードコードされた座りポーズへ切り替え、`Weld`でRootとSeatを剛体結合する。着席中はSpace押下で`Humanoid::standUp`が呼ばれ、Weldを解除して離脱する。ネットワーク時も離席はHost権威で確定し、Clientの要求はHost ACKまで再送される。

見た目・衝突形状は`Cube`と完全に同じ(Box、Decal/Texture対応込み)で、専用ジオメトリは持たない。

## 継承

着席時の Root 配置と SeatWeld 登録は一つの物理トランザクションとして処理し、body 原点を member ワールド座標として扱わない。

`Instance` → `Spatial` → `BaseCube` → `Cube` → `Seat`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Steer` | `float` (-1..1) | A:-1, D:1, どちらもor両方:0。着席中のHumanoidが毎フレーム書き込む。Lua読取専用 |
| `Throttle` | `float` (-1..1) | W:1, S:-1, どちらもor両方:0。同上 |
| `m_occupant`(private) | `weak_ptr<Humanoid>` | 同時着席防止のガード。Lua非公開 |

Steer/Throttleはライブ入力値であり設計値ではないため、YAMLへは保存されない(`noYaml()`)。

## メソッド

| メソッド | 説明 |
|---|---|
| `isOccupied()` | `m_occupant`が有効か |
| `setOccupant(h)` / `clearOccupant()` | 着席/離脱時に`Humanoid::sitOn`/`standUp`から呼ばれる |
| `sit(humanoid)` | Luau: `seat:sit(humanoid)` でHumanoidを着席位置へテレポートして着席させる。`seat:sit(nil)`で着席者を降ろす。占有中のSeatへは何もしない(警告ログ)。対象が別Seatに着席中なら先に降ろしてから移る |
| `onNativeTouched(other)` | 足のTouchで自動着席(内部用) |
| `IsA(className)` | `"Seat"`, `"Cube"`, `"BaseCube"`, ... に対して true |
| `clone()` | 位置・サイズ・色等をコピー。`m_occupant`は複製しない(新規シートは空席) |

## 依存関係

- `Cube`（描画・IsAチェーンをそのまま継承。Renderer.cppの変更は不要）
- `Humanoid`（前方宣言のみ。`sitOn`/`standUp`/着席中のSteer/Throttle更新の実体）
- `Weld`（着席時にRoot-Seat間へ動的生成される剛体結合）
- `BaseCube::setNativeTouchObserved` / `onNativeTouched`（内部Touch購読。`Box3DPhysicsBackend::dispatchContactEvents`が呼ぶ）

## 継承クラス

なし
