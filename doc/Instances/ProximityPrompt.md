# ProximityPrompt

`include/Instances/ProximityPrompt.hpp`

プレイヤーが近づいてキーを押すことでインタラクトできる UI（Roblox の ProximityPrompt 相当）。`BillboardGui` の外見・射影を流用し、距離判定・キー入力・長押し進捗を独自に管理する。

## 継承
`Instance` → `WorldGuiObject` → `BillboardGui` → `ProximityPrompt`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `KeyboardKeyCode` | `string` | トリガーキー（既定 "E"） |
| `HoldDuration` | `float` | 長押し秒数。0 なら即トリガー |
| `MaxActivationDistance` | `float` | 有効化される最大距離（既定 10.0） |
| `Enabled` | `bool` | 有効/無効 |
| `ActionText` | `string` | 操作説明テキスト（既定 "Interact"） |
| `ObjectText` | `string` | 対象物名テキスト |
| `Triggered` | `shared_ptr<RCBNScriptSignal>` | トリガー成立時に発火 |
| `m_elapsedTime` | `float` | 長押し経過時間（内部状態） |
| `m_isHolding` | `bool` | キー押下中か（内部状態） |
| `m_hasTriggered` | `bool` | 当該押下で既にトリガー済みか（内部状態） |
| `m_lastUpdateTime` | `double` | 前フレームの `glfwGetTime()`（dt 計算用） |

## メソッド
| メソッド | 説明 |
|---|---|
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用 |
| `clone()` | 自身と子を複製（`BillboardGui`→`WorldGuiObject` 分も集約） |

## フロー（`Renderer_GUI.cpp` 内、毎フレーム）

```
Cube の子として WorldGuiObject を走査
  → ProximityPrompt と判定
    → Enabled == false または isPlaying == false → スキップ
    → dist(player, cube) > MaxActivationDistance
        → 状態リセット（elapsedTime/isHolding/hasTriggered = 初期値）して continue
    → KeyboardKeyCode を GLFW キーコードへ変換
    → viewportFocused かつキー押下中
        → m_isHolding = true
        → hasTriggered が false の間 elapsedTime += dt
        → HoldDuration <= 0 または elapsedTime >= HoldDuration
            → hasTriggered = true → Triggered->fire()
    → キー未押下 → 状態リセット
  → worldToScreen() で画面座標に射影
  → ImGui で背景パネル + ObjectText/ActionText + 長押しプログレスバー描画
```

## 依存関係
- `BillboardGui`, `Named`, `RCBNScriptSignal`
- `Renderer_GUI.cpp`（判定・描画本体）、`SystemState`（`isPlaying`/`viewportFocused`/`inputState`）
- `User`/`Humanoid`（プレイヤー位置取得）、GLFW（キー入力）

## 継承クラス
なし
