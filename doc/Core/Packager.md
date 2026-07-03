# Packager

`include/Core/Packager.hpp`

ゲームを配布用フォルダとして書き出す静的パッケージャ。シーン YAML を解析してアセット参照を収集し、コピー・Luau スクリプトのバイトコード事前コンパイル・パス書き換え・ランタイム実行ファイル一式の同梱までを一括で行う。

## 構造体

```cpp
struct Config {
    std::string gameName;
    std::string outputDir;
    std::string scenePath;
    std::string engineExePath;
};
```

## メソッド

| メソッド | 説明 |
|---|---|
| `package(cfg, log)` | 静的関数。`cfg` に従ってパッケージを生成し、進捗を `log` コールバックへ逐次通知する。成功時 `true` |

## パッケージングフロー

```
Packager::package(cfg, log)
  1. 出力先 {outputDir}/{gameName} 以下に assets/{image,sound,scripts,scenes} を作成
  2. src/*.glsl をコピー（Renderer がシェーダーを相対パスで探すため）
  3. scenePath の YAML をパース
  4. ContentPath / Texture / FacePath / SkyboxPaths を再帰的に収集
  5. 収集したファイルごとに:
       .luau/.lua → luau_compile() で .luauc にインプロセス変換（失敗時はソースをコピー）
       それ以外   → 拡張子で assets/{image,sound,scripts} に振り分けてコピー
  6. YAML 内の旧パスを新しい相対パスへ書き換えて assets/scenes/{gameName}.yaml に出力
  7. RecubinEngine.exe（無ければ渡された engineExePath 自体）・launcher.exe・
     同ディレクトリの全 DLL をコピー
  8. startup.yaml（GameName / StartScene）と README.txt を出力
```

## 依存関係

- Luau コンパイラ（`luau_compile`, `include/luau/luacode.h`）
- yaml-cpp
- `std::filesystem`

## 使われる場所

- エディターのパッケージング機能（ビルド/エクスポート UI）から `Packager::package()` が呼ばれる
