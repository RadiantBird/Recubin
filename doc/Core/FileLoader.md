# FileLoader

`include/Core/FileLoader.hpp`

ファイル I/O の統一インタフェース。テキスト・バイナリの両形式に対応する静的ユーティリティクラス。

## メソッド（すべて static）

| メソッド | 戻り値 | 説明 |
|---|---|---|
| `readText(filePath)` | `string` | テキストファイルを全文読み込む |
| `readBinary(filePath)` | `vector<char>` | バイナリファイルをバイト列で読み込む |

## 依存関係

標準ライブラリのみ（`<fstream>`, `<string>` 等）

## 使われる場所

- `Renderer::loadShaderSource()` で GLSL ファイルを読み込む
- `SceneLoader::loadScene()` で YAML ファイルを読み込む
- `LuauEngine` でスクリプトファイルを読み込む
