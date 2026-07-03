# Tool

`include/Instances/Tool.hpp`

キャラクターが装備できる道具インスタンス。`Handle`（`BaseCube`）を1つ持ち、Userの装備状態に応じて `Equipped` が切り替わる。`Hand`で左右/両手持ちを指定し、`Activated`シグナルで使用アクションを通知する。HumanoidはTool自体を知らず、Userが腕ポーズ上書き指示（`leftArmRaised`/`rightArmRaised`）としてHumanoid.moveへ渡す。

## 継承
`Instance` → `Tool`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Equipped` | `bool` | 装備中かどうか |
| `Hand` | `enum class ToolHand{Right,Left,Both}` | 持ち手指定 |
| `Activated` | `shared_ptr<RCBNScriptSignal>` | 使用アクション時に発火 |
| `Handle` | `shared_ptr<BaseCube>` | 手に持たれるパーツ（`resolveHandle()`で解決） |
| `m_handleName` | `string` | Handle参照名。制約(Motor等)の`m_cube0Name`と同じ規約で保存・遅延解決 |

## メソッド

| メソッド | 説明 |
|---|---|
| `getClassName()` | `"Tool"` を返す |
| `IsA(className)` | 継承チェーンを含む型チェック |
| `setProperty(name, value)` | `Hand`(文字列→enum)、`Handle`(名前保存→`resolveHandle()`)を処理 |
| `onAncestorChanged()` | 祖先確定後、未解決のHandleを`resolveHandle()`で再試行してから基底処理を呼ぶ |
| `resolveHandle()`（private） | `m_handleName`から`Handle`を解決。Workspace配下ならWorkspace起点、そうでなければ最上位祖先起点で`getChildByPath` |

## 依存関係

- `BaseCube`（Handle）
- `RCBNScriptSignal`（Activated）
- `User`（装備操作元）

## 継承クラス

なし
