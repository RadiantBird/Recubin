# LuarCompiler

`include/Core/LuarCompiler.hpp`

`.luar` ソースを Luau ソースへ変換する外部トランスパイラ（Rust 製 `luar-rs` クレート）の DLL ラッパー。`luar_compiler.dll` を動的ロードし、エクスポート関数をアドレス解決して呼び出す。

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `m_dll` | `HMODULE` | ロード済み DLL ハンドル |
| `m_fnCompile` | `FnCompile`（`int(*)(const char*, char*, size_t)`） | `luar_compile` エクスポート関数ポインタ |
| `m_fnGetErrors` | `FnGetErrors`（`int(*)(char*, size_t)`） | `luar_get_errors` エクスポート関数ポインタ |

## メソッド

| メソッド | 説明 |
|---|---|
| `LuarCompiler()` | `luar_compiler.dll` をロードし、`luar_compile` / `luar_get_errors` を解決する。失敗時は `m_dll = nullptr` のままにしてログを出す |
| `~LuarCompiler()` | `FreeLibrary()` で DLL を解放 |
| `compile(luarSource)` | `.luar` ソースを Luau ソース文字列に変換。DLL 未ロード時・コンパイル失敗時は空文字列を返し、失敗時は `luar_get_errors` でエラーメッセージを取得してログ出力する |
| `isLoaded()` | DLL のロードに成功しているか（`m_dll != nullptr`） |

## 変換フロー

```
LuarCompiler::compile(luarSource)
  1. 出力バッファ 1MB を確保
  2. m_fnCompile(src, buf, bufSize) を呼ぶ
  3. 戻り値 != 0 → m_fnGetErrors() でエラー文字列取得 → ログ出力 → "" を返す
  4. 戻り値 == 0 → buf を std::string として返す
```

## 依存関係

- `luar_compiler.dll`（実行ファイルと同じディレクトリに配置される外部 DLL、Rust 製）
- Windows API（`LoadLibraryA` / `GetProcAddress` / `FreeLibrary`）

## 使われる場所

- `.luar` 拡張子のスクリプトを実行・パッケージングする際に、Luau へ変換するために呼ばれる
