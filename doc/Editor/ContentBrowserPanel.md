# ContentBrowserPanel

`include/Editor/ContentBrowserPanel.hpp`

`assets/` ディレクトリをブラウズできるファイルブラウザパネル。

## 継承

`EditorPanel` → `ContentBrowserPanel`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `currentPath` | `filesystem::path` | 現在表示しているディレクトリパス |

## メソッド

| メソッド | 説明 |
|---|---|
| `onRender()` | フォルダ内容を ImGui でリスト表示し、クリックでナビゲート |
| `drawDirectory(path)` *(private)* | ディレクトリを再帰的に描画 |

## 依存関係

- `EditorPanel`, `<filesystem>`, ImGui
