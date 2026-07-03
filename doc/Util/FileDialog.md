# FileDialog

`include/Util/FileDialog.hpp`

Windows COM の `IFileOpenDialog` を使ったファイル選択ダイアログのラッパー関数。

## メソッド

| メソッド | 説明 |
|---|---|
| `browseFile(filterName, filterSpec)` | ファイルを開くダイアログを表示し、選択されたパスを UTF-8 文字列で返す。キャンセル/失敗時は空文字列 |

## 引数

| 引数 | 型 | 説明 |
|---|---|---|
| `filterName` | `const wchar_t*` | フィルタの表示名（例: `L"Model (*.yaml)"`） |
| `filterSpec` | `const wchar_t*` | フィルタのパターン（例: `L"*.yaml"`） |

## 動作フロー

```
browseFile()
  CoCreateInstance(CLSID_FileOpenDialog)
  SetFileTypes(filterName, filterSpec)
  Show(nullptr) → GetResult() → GetDisplayName(SIGDN_FILESYSPATH)
  ワイド文字列をUTF-8へ変換して返す
```

## 依存関係

- Windows API（`shobjidl.h`, COM）

## 使われる場所

- コンテンツブラウザ/エディターパネル等、ファイル選択が必要な箇所から呼ばれる
- 類似のダイアログ実装が `AnimationEditorPanel.cpp` にもローカル関数として存在（Export/Import 用、保存ダイアログにも対応）
