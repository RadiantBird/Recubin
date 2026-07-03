# GuiButton

`include/Instances/GuiButton.hpp`

クリック可能な画面空間 GUI 要素の基底クラス。`ScreenGuiObject` にクリック検出用の `Activated` シグナルを追加する。単体では見た目を持たず、`TextButton`/`ImageButton` が具体的な描画内容を提供する。

## 継承
`Instance` → `ScreenGuiObject` → `GuiButton`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `Activated` | `shared_ptr<RCBNScriptSignal>` | クリック時に発火するシグナル |
| `m_wasClickedThisFrame` | `bool` | クリック判定のエッジ検出用（Luau 非公開） |

## メソッド
| メソッド | 説明 |
|---|---|
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | `ScreenGuiObject::setProperty` へ委譲（独自プロパティなし） |

## 依存関係
- `ScreenGuiObject`, `Named`（CRTP で `getClassName()` を自動反映）
- `Renderer.hpp` の `m_onButtonActivated` コールバック（クリック時に `LuauEngine::onGuiButtonActivated` を呼び出す）

## 継承クラス
- `TextButton`（テキスト付きボタン）
- `ImageButton`（画像付きボタン）
