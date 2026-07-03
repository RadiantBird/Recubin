# ScreenGuiObject

`include/Instances/ScreenGuiObject.hpp`

画面空間（スクリーンスペース）に描画される 2D GUI 要素の基底クラス。位置・サイズ・背景色・表示状態を持ち、ImGui の `WindowDrawList` に直接矩形として描画される。3D シーンとは独立したオーバーレイ。

## 継承
`Instance` → `ScreenGuiObject`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `Active` | `bool` | 入力を受け付けるか |
| `Position` | `Vector2` | 画面上の位置 |
| `Size` | `Vector2` | サイズ（既定 100x40） |
| `NormType` | `Norm` | `Pixel`/`Scale`（座標の単位系） |
| `Visible` | `bool` | 表示/非表示 |
| `BackgroundColor` | `Color4` | 背景色（アルファ = 不透明度） |
| `ZIndex` | `int` | 描画順 |
| `FontSize` | `float` | 文字サイズ。0 = 既定サイズ（TextLabel/TextButton のみ使用） |
| `Hovered` | `shared_ptr<RCBNScriptSignal>` | マウスカーソルが要素内に入った瞬間に発火（Roblox の MouseEnter 相当） |
| `m_wasHovered` | `bool` | ホバー判定のエッジ検出用（Luau 非公開） |

## メソッド
| メソッド | 説明 |
|---|---|
| `getTransparency()`/`setTransparency(t)` | `BackgroundColor.a` を透明度として読み書き |
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用 |

## 依存関係
- `Vector2`, `Color4`, `RCBNScriptSignal`
- `Renderer_GUI.cpp`（`renderScreenGui` で ImGui 描画）

## 継承クラス
- `GuiButton`（クリック機能を追加）
- `TextLabel`（テキスト表示）
- `ImageLabel`（画像表示）
