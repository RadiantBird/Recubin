# Packager

`include/Core/Packager.hpp`

ゲームを配布用パッケージとして書き出す静的パッケージャ。シーン YAML を解析してアセット参照を収集し、コピー・Luau スクリプトのバイトコード事前コンパイル・パス書き換え・ゲームランタイムの同梱までを一括で行う。

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

## 出力形式

Windowsでは従来どおり、`{outputDir}/{gameName}` のフォルダを出力する。フォルダ直下に
`RecubinEngine.exe`、必要なDLL、任意の`launcher.exe`、`startup.yaml`、`README.txt`、
`assets/`、`shaders/`を配置する。`RecubinEngine.exe`がエディターの隣にない場合は、
エディターを代わりに同梱せずパッケージを失敗させる。

`assets/fonts/`はシーンからの参照有無にかかわらず、エンジン標準のランタイムリソースとして
必ず同梱する。これにより、TextLabel／TextButtonの字形と文字幅、BillboardGui内での縮小結果が
エディターとランタイムで一致する。必須の`DotGothic16-Regular.ttf`が見つからない場合は、
異なるフォントへ暗黙にフォールバックせずパッケージを失敗させる。

macOSでは標準App Bundleとして `{outputDir}/{gameName}.app` を出力する。

```text
{gameName}.app/
└─ Contents/
   ├─ Info.plist
   ├─ MacOS/
   │  └─ RecubinEngine
   └─ Resources/
      ├─ startup.yaml
      ├─ README.txt
      ├─ shaders/
      └─ assets/
```

macOSでも、同梱する実行ファイルはエディターではなく、エディターの隣にある
`RecubinEngine`です。`launcher.exe`やDLLはApp Bundleへコピーしません。
Finderからは`{gameName}.app`をダブルクリックして起動できます。ランタイムは実行ファイルの
位置から`Contents/Resources`を検出して作業ディレクトリにするため、通常の相対パス
（`assets/...`、`shaders/...`）をそのまま使用します。
すべてのファイルを配置した後、macOS標準の`codesign --sign -`でBundle全体へad-hoc署名を
付与し、厳格検証も行う。これにより実行ファイルだけに残る署名とBundleのResourcesの不整合を
防ぎ、FinderがApp Bundleを正しく認識できるようにする。Developer ID署名や公証は含まれないため、
インターネット配布時のGatekeeper警告を解消するには別途署名・公証が必要となる。

シーンのルート直下にある最初の`AppImage`の`IconPath`が指定されている場合、Recubin内蔵の
ICNS writerが画像を読み込み、縦横比を維持したRGBA PNGエントリを生成して
`Contents/Resources/AppIcon.icns`へ直接書き込みます。`sips`、`iconutil`、Homebrew、Xcode、
Command Line Toolsなどの外部ツールは使用しません。画像の読み込み、PNG生成、ICNS書き込み、
または検証に失敗した場合は原因をログへ出してパッケージを失敗させます。`AppImage`がない場合は
`Info.plist`へカスタムアイコンを設定せず、macOS標準アイコンを使用します。`IconPath`の元画像
自体は従来どおり`Resources/assets/...`へコピーされます。

## パッケージングフロー

```
Packager::package(cfg, log)
  1. 出力先（Windowsはフォルダ、macOSはApp BundleのContents/Resources）に assets/{image,sound,scripts,scenes} を作成
  2. assets/fonts/ と shaders/*.glsl をコピー（Renderer が相対パスで探すため）
  3. scenePath の YAML をパース
  4. ContentPath / Texture / FacePath / MeshFile / IconPath / SkyboxPaths を再帰的に収集
  5. 収集したファイルごとに:
       .luau/.lua → luau_compile() で .luauc にインプロセス変換（失敗時はソースをコピー）
       それ以外   → 拡張子で assets/{image,sound,scripts} に振り分けてコピー
  6. YAML 内の旧パスを新しい相対パスへ書き換えて assets/scenes/{gameName}.yaml に出力
  7. エディターの隣にあるRecubinEngine（Windowsは.exe）をコピー。見つからない場合は失敗
     （Windowsのみlauncher.exeと同ディレクトリの全DLLもコピー）
  8. startup.yaml（GameName / StartScene）と README.txt を出力
  9. macOSではInfo.plistを生成し、ルートAppImageがあれば内蔵writerでAppIcon.icnsを生成
 10. macOSでは完成したApp Bundle全体をad-hoc署名し、厳格検証する
```

## 依存関係

- Luau コンパイラ（`luau_compile`, `include/luau/luacode.h`）
- yaml-cpp
- `std::filesystem`

## 使われる場所

- エディターのパッケージング機能（ビルド/エクスポート UI）から `Packager::package()` が呼ばれる
