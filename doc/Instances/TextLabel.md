# TextLabel

`include/Instances/TextLabel.hpp`

テキストを表示するだけの画面空間 GUI（クリック不可）。`ScreenGuiObject` にテキスト内容と文字色を追加する。

## 継承
`Instance` → `ScreenGuiObject` → `TextLabel`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `Text` | `string` | 表示文字列 |
| `TextColor` | `Color4` | 文字色（既定は黒不透明） |

## メソッド
| メソッド | 説明 |
|---|---|
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用（`FontSize` は `ScreenGuiObject` のフィールドをここで再登録） |
| `clone()` | 自身と子を複製（`ScreenGuiObject` 分も集約） |

## 依存関係
- `ScreenGuiObject`, `Named`
- `Renderer_GUI.cpp`（`renderScreenGui` で ImGui テキスト描画）

## 継承クラス
なし
