# Recubin C++ SDK API リファレンス（アーカイブ）

この文書は旧静的埋め込み SDK の記録です。現在の公開 API は Process DLL の
`start()`、任意の `stop()` / `bindHost()`、`FindChild`、`FindChildWithClass`、
`task::wait()` です。詳細は `app/include/recubin_process_api.h` を参照してください。

この文書は、`#include <recubin.h>` から利用する公開 API の契約を説明します。対象は Windows / MSVC の静的 SDK です。公開ヘッダーは既存の `Instance`、`Workspace`、`Cube`、`Model`、`StringValue`、`Vector3`、`CFrame` も再公開するため、これらの型を使うコードは SDK の include tree と同じ C++23 / MSVC ランタイム設定でコンパイルしてください。

## 最小ライフサイクル

```cpp
#include <recubin.h>
#include <memory>

int main() {
    recubin::Engine engine;
    recubin::EngineConfig config;
    config.scenePath = "sample_scene.yaml";
    config.visible = true;

    if (!engine.initialize(config)) {
        // engine.lastError() を記録する
        return 1;
    }

    auto workspace = engine.workspace();
    if (!workspace) {
        // 初期化成功後でも、必須の Workspace がない場合は処理を中止する
        engine.shutdown();
        return 1;
    }
    auto cube = std::make_shared<Cube>(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(2.0f, 2.0f, 2.0f),
        TextureHandle{});
    cube->Name = "HostCube";
    workspace->addChild(cube);

    if (!engine.step(1.0f / 60.0f)) {
        // false はエラーだけでなく、閉じる／停止要求も表す。
        // 失敗として扱う場合は lastError() を確認する。
        if (!engine.lastError().empty()) {
            engine.shutdown();
            return 1;
        }
    }

    // step() と run() は通常どちらか一方を選ぶ。ここでは run() の戻り値も
    // 確認するため、停止を要求して即時に run() を終了させている。
    engine.requestStop();
    const int result = engine.run();
    cube.reset();
    workspace.reset();
    engine.shutdown();
    return result;
}
```

`Engine` はコピー・ムーブできません。通常は一つの `Engine` を生成し、`initialize()`、`step()` または `run()`、`shutdown()` の順に使います。明示的な `shutdown()` を省略してもデストラクタが呼びますが、保持している `Workspace` や Instance を破棄するタイミングを明確にするため、所有者側で呼び出すことを推奨します。

## EngineConfig

| フィールド | 型 | 既定値 | 契約 |
|---|---|---:|---|
| `scenePath` | `std::string` | `""` | 起動時に読み込むシーン。空文字列でも空のシーンをロードする。相対パスはプロセスの現在作業ディレクトリ基準で解決される。 |
| `windowTitle` | `std::string` | `"Recubin"` | GLFW ウィンドウのタイトル。 |
| `windowWidth` | `int` | `1280` | GLFW が受け取るウィンドウ幅（スクリーン座標単位）。正の値を指定する。高 DPI 環境の framebuffer 物理ピクセル数と同じとは限らない。 |
| `windowHeight` | `int` | `720` | GLFW が受け取るウィンドウ高さ（スクリーン座標単位）。正の値を指定する。高 DPI 環境の framebuffer 物理ピクセル数と同じとは限らない。 |
| `visible` | `bool` | `true` | `false` の場合もウィンドウと OpenGL コンテキストは作成される。完全なヘッドレス実行や GPU 非依存実行を意味しない。 |

シーンを読み込む場合、シーンから参照されるアセットに加えて、標準フォントと `shaders/` が実行時の作業ディレクトリから解決できる必要があります。SDK のサンプル CMake はこれらを実行ファイルの隣へコピーします。

## Engine のメソッド

### `bool initialize(const EngineConfig& config = {})`

GLFW、OpenGL、GLEW、Renderer、AudioService、Luau、入力、物理、ランタイム用 GUI を初期化し、`config.scenePath` をロードします。成功時は `isInitialized() == true` となり、`workspace()` が使用できます。

失敗時は `false` を返し、`lastError()` に失敗理由を設定します。ウィンドウ作成、GLEW、音声、シーンロードの失敗では、作成済みのランタイム資源を解放して未初期化状態へ戻ります。同一プロセスで既に別の Engine が初期化済みなら失敗します。

既に初期化済みの Engine に対して呼び出すと、現在のランタイムを先に shutdown してから再初期化します。初期化に成功した Engine を複数同時に保持する契約はありません。

### `bool loadScene(const std::string& scenePath)`

初期化済み Engine の現在のシーンを置き換えます。まず隔離された Stage へロードし、ロードに成功した場合だけ現在のタスク、Terrain streamer、Workspace の物理を解放して Commit します。

失敗時は `false` を返し、`lastError()` に理由を設定します。Stage のロードに失敗した段階では、現在の Workspace は維持されます。成功時は `workspace()` が新しい Workspace を返すため、以前に取得した `shared_ptr<Workspace>` を新しいシーンの参照として再利用しないでください。

### `std::shared_ptr<Workspace> workspace() const noexcept`

現在の Workspace を返します。Engine が未初期化、shutdown 済み、またはロード前の場合は `nullptr` になり得ます。返された `shared_ptr` は Instance ツリーを保持しますが、Engine の内部ランタイム、物理、Terrain、Renderer の所有権を移しません。

Workspace に追加する Instance は `std::shared_ptr` で作成し、`addChild()` に渡します。Instance の `Parent` は `std::weak_ptr`、親の `children` は `std::shared_ptr` で保持されます。`getChild()` / `getChildByPath()` の戻り値は所有権を持たない raw pointer なので、子の削除、親の破棄、またはシーン切り替え後は無効になり得ます。長く保持する必要がある場合は、必要な寿命を呼び出し側の `std::shared_ptr` で明示してください。Engine の `loadScene()` 成功後、以前の Workspace やその子を新しいシーンへ移すことはサポート対象ではありません。

### `bool step(float deltaSeconds)`

一フレームを進めます。イベント処理、物理、Luau の Heartbeat / update / Script 実行、入力、Humanoid / Terrain / Weather / Particle 更新、描画、音声更新を、Recubin ランタイムと同じ順序で実行します。

`deltaSeconds` は秒単位の有限値で、負数は無効です。無効値、未初期化状態、ウィンドウ終了、Luau の安全停止、User の終了要求では `false` を返します。`requestStop()` 後も stop flag を見てフレームを事前にスキップすることはなく、その呼び出しのフレーム処理を行ってから `false` を返します。`false` の後に同じ Engine で処理を続ける契約はありません。`lastError()` には過去の失敗の文字列が残る場合もあるため、その内容だけでは今回の `false` がエラーか正常停止かを常に判別できません。呼び出し側で停止要求とエラーの履歴を管理してください。

### `int run()`

ウィンドウが閉じられるか `requestStop()` が呼ばれるまで、`step()` を実時間の差分（最大 0.25 秒）で繰り返します。stop flag が既に立っている場合は `step()` を一度も呼ばずに終了します。戻り値 `0` は `lastError()` が空のままループを終了したことを示し、`-1` は未初期化またはエラー状態を示します。`run()` 終了後に同じ Engine を再開するには `initialize()` で再初期化してください。

OpenGL / GLFW のコンテキストを作成したスレッドで呼び出してください。`run()` はブロッキングするため、ホスト側で独自ループを管理する場合は `step()` を使います。

### `void requestStop()`

実行ループの停止フラグを設定します。停止要求はスレッドセーフな atomic 状態で保持されるため、`run()` を実行しているスレッドとは別のスレッドから呼べます。ウィンドウ、Workspace、Renderer などの操作を別スレッドから行うことはできません。

### `void shutdown()`

Luau タスク、Terrain streamer、物理、Renderer、Audio、ウィンドウ、GLFW を定義された順序で解放し、Engine を未初期化状態へ戻します。複数回呼び出しても安全です。`shutdown()` は stop flag や `lastError()` をクリアしません。`shutdown()` 後に `workspace()` が以前返した Instance を描画・物理・Luau に渡したり、GL 関連資源へアクセスしたり、その破棄を Engine が保証するものとして扱わないでください。

### 状態取得

- `isInitialized()` は、初期化が完了し shutdown されていない場合に `true`。
- `lastError()` は直近の失敗理由を返す。成功した `loadScene()` はエラー文字列をクリアする。成功した `step()` はエラーを自動的に消すとは限らないため、失敗直後に確認する。

## スレッドと所有権

Engine は一つの GLFW / OpenGL コンテキストを所有します。初期化、`step()`、`run()`、`loadScene()`、`shutdown()`、Workspace と Instance の変更は同じホストスレッドから呼んでください。別スレッドから許可される操作は、`run()` 中の `requestStop()` だけです。

Engine は内部の Renderer、AudioService、Luau、User、System、物理を所有します。外部が所有する `shared_ptr` は Instance の保持に使えますが、内部サービスの寿命やスレッド安全性を延長しません。複数の Engine を同時に初期化すること、外部の GLFW ウィンドウや OpenGL コンテキストを渡すこと、Studio の Editor パネルを SDK API として利用することはサポート対象外です。`Parent` の weak reference、親の `children` が保持する shared ownership、`getChild()` の raw borrow を混同しないでください。

## ビルドとリンク

SDK の生成、CMake 利用、MSVC の手動リンク、フォント・シェーダー配置は [Embedding.md](Embedding.md) を参照してください。CMake 利用時は `find_package(Recubin CONFIG REQUIRED)` と `Recubin::Runtime` を使います。SDK の静的アーカイブは Windows/MSVC の同一ランタイム設定（通常 `/MD`）で利用してください。

