# Tool

`include/Instances/Tool.hpp`

キャラクターが装備できる道具モデル。`Handle`（`BaseCube`）を1つ持ち、Userの装備状態に応じて `Equipped` が切り替わる。`Hand`で左右/両手持ちを指定し、`Activated`シグナルで使用アクションを通知する。HumanoidはTool自体を知らず、Userが腕ポーズ上書き指示（`leftArmRaised`/`rightArmRaised`）としてHumanoid.moveへ渡す。

## 継承
Tool は `Model` として自身の local CFrame と子部品の相対姿勢を保持する。装備時は Handle のワールド CFrame を直接書き換えず、手と Handle の間に一時的な `ToolGrip` Weld を作成する。Grip の規約は `armWorld * GripC0 == handleWorld * GripC1` であり、装備解除時にこの Weld を破棄する。通常の Weld の作成時相対姿勢は変更しない。

したがって新形式の `Position` / `Rotation` は通常の Model local CFrame であり、Grip オフセットではない。旧形式で `GripC0`/`GripC1` が存在しない Tool だけは、旧 `Position` / `Rotation` を Handle 側の Grip オフセットへ移行する。

`Instance` → `Spatial` → `Model` → `Tool`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Equipped` | `bool` | 装備中かどうか |
| `Hand` | `enum class ToolHand{Right,Left,Both}` | 持ち手指定 |
| `GripC0` | `CFrame` | 手側の Grip フレーム。既定値は手前 1 stud (`(0,0,-1)`) |
| `GripC1` | `CFrame` | Handle 側の Grip フレーム。既定値は identity |
| `Position` / `Rotation` | `Model` 継承 | 新形式では Tool 自身の local CFrame |
| `Activated` | `shared_ptr<RCBNScriptSignal>` | 使用アクション時に発火 |
| `Handle` | `shared_ptr<BaseCube>` | 手に持たれるパーツ（`resolveHandle()`で解決） |
| `m_handleName` | `string` | Handle参照名。制約(Motor等)の`m_cube0Name`と同じ規約で保存・遅延解決 |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"Tool"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | `GripC0`/`GripC1`、`Hand`(文字列→enum)、`Handle`(名前保存→`resolveHandle()`)を処理。旧形式の `Position`/`Rotation` は Grip オフセットへ移行 |
| `onAncestorChanged()` | 祖先確定後、未解決のHandleを`resolveHandle()`で再試行してから基底処理を呼ぶ |
| `resolveHandle()`（private） | `m_handleName`から`Handle`を解決。Workspace配下ならWorkspace起点、そうでなければ最上位祖先起点で`getChildByPath` |

## 依存関係

- `BaseCube`（Handle）
- `RCBNScriptSignal`（Activated）
- `User`（装備操作元）

## 継承クラス

なし
