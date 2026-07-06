# Seat

`include/Instances/Seat.hpp`

座席。RobloxのVehicleSeat相当。`Humanoid`が`Physics::findOverlapping(*Root, "Seat")`で接触を検知すると、未占有であれば自動的に着席する(`Humanoid::sitOn`): Rootをシート直上へスナップし、ハードコードされた座りポーズへ切り替え、`Weld`でRootとSeatを剛体結合する。着席中はSpace押下で`Humanoid::standUp`が呼ばれ、Weldを解除して離脱する。

見た目・衝突形状は`Cube`と完全に同じ(Box、Decal/Texture対応込み)で、専用ジオメトリは持たない。

## 継承

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
| `IsA(className)` | `"Seat"`, `"Cube"`, `"BaseCube"`, ... に対して true |
| `clone()` | 位置・サイズ・色等をコピー。`m_occupant`は複製しない(新規シートは空席) |

## 依存関係

- `Cube`（描画・IsAチェーンをそのまま継承。Renderer.cppの変更は不要）
- `Humanoid`（前方宣言のみ。`sitOn`/`standUp`/着席中のSteer/Throttle更新の実体）
- `Weld`（着席時にRoot-Seat間へ動的生成される剛体結合）
- `Physics::findOverlapping`（接触判定）

## 継承クラス

なし
