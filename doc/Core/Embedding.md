# C++ 静的ライブラリ利用

Windows / MSVC では、インストール済み SDK の `include/recubin.h` と `Recubin.lib` を C++23 プロジェクトから利用できます。CMake を使う場合は SDK の `lib/cmake/Recubin` を `CMAKE_PREFIX_PATH` に追加し、`find_package(Recubin CONFIG REQUIRED)` と `Recubin::Runtime` を使います。

SDK をソースツリーから生成する場合は、Windows の開発者コマンドプロンプトで次を実行します。

```powershell
cmake -S . -B build
cmake --build build --config Release --target RecubinSdkArchive
cmake --install build --config Release --prefix C:/path/to/recubin-sdk
```

`RecubinSdkArchive` は依存アーカイブを統合した `Recubin.lib` を生成します。`cmake --install` は `include/`、`lib/Recubin.lib`、標準フォント、シェーダー、`examples/embedding` を SDK に配置します。

```cpp
#include <recubin.h>
#include <memory>

recubin::Engine engine;
recubin::EngineConfig config;
config.windowTitle = "My game";
if (!engine.initialize(config)) {
    // engine.lastError()
}

auto workspace = engine.workspace();
auto cube = std::make_shared<Cube>(Vector3(0.0f, 0.0f, 0.0f),
                                   Vector3(2.0f, 2.0f, 2.0f),
                                   TextureHandle{});
cube->Name = "CubeFromHost";
workspace->addChild(cube);
engine.step(1.0f / 60.0f);
engine.requestStop();
engine.run();
engine.shutdown();
```

`Engine` が GLFW ウィンドウと OpenGL コンテキストを所有します。`step()` はイベント処理、物理、Luau、入力、描画、音声を1フレーム進め、`run()` はウィンドウが閉じられるか `requestStop()` が呼ばれるまで同じ処理を繰り返します。`Engine` はコピー不可で、同一プロセス内の複数インスタンス利用はサポートしません。

SDK の `examples/embedding` は、外部プロジェクトとして `recubin.h` のインクルード、`Recubin::Runtime` のリンク、付属シーンの読み込み、Luau スクリプトによる `StringValue` の変更、Workspace への Cube 追加、`step()` と実フレームループの `run()`、終了を確認します。シーンパスを第1引数に渡すと、そのシーンと Luau スクリプトを読み込んでから実行します。`--headless` はウィンドウを非表示にします。

```powershell
cmake -S examples/embedding -B build-embedding `
  -DCMAKE_PREFIX_PATH=C:/path/to/recubin-sdk
cmake --build build-embedding --config Release
cd build-embedding/Release
./RecubinEmbeddingExample.exe --headless
```

サンプルの CMake 構成は `sample_scene.yaml` と `sample_script.luau` を実行ファイルの隣へコピーします。`run()` は初期化したメインスレッドで短時間実行し、補助スレッドから `requestStop()` を呼んで終了します。実際のアプリケーションでは、OpenGL/GLFW を使う `Engine` の API を同じスレッドから使い、`step()` または `run()` のどちらか一方を選んでください。

MSVC のツールセットとランタイム設定は SDK と外部プロジェクトで一致させてください。`Recubin::Runtime` は統合済みの `Recubin.lib` と、OpenGL / Windows のシステムライブラリをリンクします。シーンを読み込む場合は、標準フォント、シェーダー、参照アセットをランタイムの作業ディレクトリから解決できるよう配置してください。サンプル CMake は SDK の `assets/fonts` と `shaders` を実行ファイルの隣へコピーします。

CMake を使わず MSVC で直接リンクする場合は、少なくとも SDK の `include` と SDK ルート（内部の `#include <include/...>` 解決用）をインクルードパスに追加してください。ImGui の直接 API を使う場合は `include/imgui` も追加します。コンパイルには `/std:c++23 /utf-8 /MD` を使用してください。リンカーには SDK の `lib/Recubin.lib` を完全パスで指定し、登録オブジェクトを保持するため `/WHOLEARCHIVE:C:\path\to\recubin-sdk\lib\Recubin.lib` を追加します。Windows 側の必須システムライブラリは `opengl32.lib user32.lib gdi32.lib shell32.lib ws2_32.lib winmm.lib` です。

外部ウィンドウへの埋め込み、ホスト側の OpenGL コンテキスト共有、複数 `Engine` の同時実行は対象外です。Studio 固有の ImGui パネルは公開 API に含めず、ランタイムの NullEditorManager 経路だけを使用します。

