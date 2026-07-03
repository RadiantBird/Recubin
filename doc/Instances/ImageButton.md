# ImageButton

`include/Instances/ImageButton.hpp`

画像を表示するクリック可能な画面空間 GUI。`GuiButton` に画像パスとロード済みテクスチャ ID を追加する。

## 継承
`Instance` → `ScreenGuiObject` → `GuiButton` → `ImageButton`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `imagePath` | `string` | 画像ファイルパス |
| `m_textureID` | `unsigned int` | `Renderer::loadTexture()` でロードされた GL テクスチャ ID |

## メソッド
| メソッド | 説明 |
|---|---|
| `setImage(path)` | パスを保存し `Renderer::instance` 経由でテクスチャをロード |
| `getImage()` | `imagePath` を返す |
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用（`Image` プロパティは `method_prop` で `setImage`/`getImage` に接続） |
| `clone()` | 自身と子を複製（`GuiButton`→`ScreenGuiObject` 分も集約） |

## 依存関係
- `GuiButton`, `Named`
- `Renderer.hpp`（`loadTexture`）
- `Renderer_GUI.cpp`（`renderScreenGui` でのクリック判定・画像描画、`m_onButtonActivated` コールバック経由）

## 継承クラス
なし
