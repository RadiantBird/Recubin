# AppImage

`include/Instances/AppImage.hpp`

アイコン画像パスのみを保持する単純なインスタンス。他の GUI クラスとは継承関係を持たず `Instance` を直接継承する（`ScreenGuiObject`/`WorldGuiObject` 系のシーン内 GUI ではなく、ランチャー等でアプリアイコンを表すためのデータホルダー）。

## 継承
`Instance` → `AppImage`

## メンバ変数
| 変数 | 型 | 説明 |
|---|---|---|
| `iconPath` | `string` | アイコン画像ファイルへのパス |

## メソッド
| メソッド | 説明 |
|---|---|
| `getClassName()` | `"AppImage"` を返す |
| `IsA(name)` | 継承チェーンを含む型チェック |
| `setProperty(name, val)` | YAML デシリアライズ用（`IconPath` が空なら YAML 出力を省略） |
| `clone()` | 自身を複製（子を持たない想定） |

## 依存関係
- `PropertyRegistry`（`IconPath` フィールド登録、`omitEmpty()`）

## 継承クラス
なし
