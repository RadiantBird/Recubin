# IPlatform

`include/Util/IPlatform.hpp` / `include/Util/Platform.hpp`

OS依存の操作(ファイル/フォルダダイアログ、ファイルマネージャーで開く、コンソールのUTF8設定、
動的ライブラリのロード)を抽象化するインターフェイス。`IInputBackend`と同じ設計方針で、
呼び出し側はこのIF経由でのみOS機能を使い、特定OSのAPIに直接依存しない。

## アクセサ

`IPlatform& getPlatform();`（`include/Util/Platform.hpp`）— `Renderer::instance`と同様の
シングルトンアクセサ。呼び出し側は`getPlatform().openFileDialog(...)`のように使う。

## メソッド

| メソッド | 説明 |
|---|---|
| `openFileDialog(filters)` | ファイルを開くダイアログ。キャンセル/失敗時は空文字列 |
| `saveFileDialog(filters, defaultExt)` | ファイルを保存するダイアログ。`defaultExt`は拡張子未入力時に補う既定拡張子 |
| `openFolderDialog()` | フォルダ選択ダイアログ |
| `revealInFileManager(path)` | OS標準のファイルマネージャー(エクスプローラー/Finder相当)でパスを開く |
| `setupConsoleUtf8()` | 起動時に1回呼ぶ、コンソールの入出力コードページをUTF-8にする処理 |
| `loadDynamicLibrary(name)` / `getSymbol(handle, name)` / `freeDynamicLibrary(handle)` | 動的ライブラリのロード(`void*`ハンドルで抽象化) |

`FileFilter{ name, spec }`は`{"Scene (*.yaml;*.yml)", "*.yaml;*.yml"}`のようにダイアログの
ファイル種別欄を表す。複数指定すると種別セレクタに複数エントリが並ぶ。

## 実装

| クラス | ファイル | 説明 |
|---|---|---|
| `WindowsPlatform` | `include/Util/WindowsPlatform.hpp` / `src/Util/WindowsPlatform.cpp` | COMファイルダイアログ・`ShellExecuteW`・`SetConsoleOutputCP`・`LoadLibraryA`等を集約した実装 |
| `MacPlatform` | `include/Util/MacPlatform.hpp` / `src/Util/MacPlatform.mm` | Cocoaの`NSOpenPanel`/`NSSavePanel`/`NSWorkspace`と`dlopen`/`dlsym`/`dlclose`を使うmacOS実装 |
| `MockPlatform` | `include/Util/MockPlatform.hpp` | OS非依存のスタブ実装。ダイアログ系は空文字列、その他はno-op |

`getPlatform()`はWindowsでは`WindowsPlatform`、macOSでは`MacPlatform`、それ以外では
`MockPlatform`を返す。環境変数`RECUBIN_MOCK_PLATFORM`が設定されている場合は、OSに関係なく
最優先で`MockPlatform`へ切り替える。

macOSのファイルダイアログは`FileFilter.spec`の`;`区切りパターンを拡張子として扱い、
`*.*`は種類を制限しない。UI操作はCocoaのメインスレッドで実行される。

## 使われる場所

- シーンを開く/保存する・Terrainのデータパス選択・アセット参照(GLB/画像/音声/スクリプト)の
  ファイル選択、パッケージ出力先フォルダ選択、アニメーションのExport/Import、
  スクリプトの「外部エディタで開く」など、エディターパネル全般
- `LuarCompiler`のDLLロード
- `main.cpp`/`game_main.cpp`/`test_main.cpp`起動時のコンソールUTF8設定
