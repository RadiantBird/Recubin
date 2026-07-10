# Recubin -Powering imagination again-
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)](https://isocpp.org/)
[![Luau](https://img.shields.io/badge/Luau-Language-007ACC?logo=luau)](https://luau-lang.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.1-E65226?logo=opengl)](https://www.opengl.org/)

![logo](Recubin.png)

RecubinはC++で作成されたゲームエンジンで、
楽しい物理エンジン、Luauでコーディング、複数Workspaceなど、
ゲーム開発を楽しくしてくれる要素がたくさんです。
Windowsで動作します。後々、Mac対応予定。

## 目標
- 誰でも簡単にゲームを開発し、ローカルで公開できるように。
- Robloxでできなかったことを実現する。
- わかりやすいエディターを作る。直感的であるように。
- 物理で遊べるようにする。

## Todoリスト
(!: 中止|?: 不明|~: 保留中|x: 達成済み)

### 設計関係
- ImageLabel/ImageButton
  - [~] IsAだと不自然なのでHasAにリファクタ

### エディター関係
- [ ] エディター自体のパッケージャー(デプロイ)の作成
  - Pythonで可
  - フォルダ
  - 根本的な依存ファイルを整理して移動させておく(現状は丸投げファイルが多い)
  - DLLコピー
  - exeコピー
  - imgui.iniコピー

- [ ] アタッチメントの位置移動などがコマンド履歴に保存されていない問題の修正

- [x] LocalScript実装(v2.0ネットワーク基盤の一部として最小実装。詳細はprogress.md参照)
      - [x] Script派生の最小クラスを追加。UseNetworkがfalseならScriptと同じ扱い
      - [x] UseNetwork=true時、Client側ではLocalScriptのみ実行(Scriptは実行しない)
      - [x] エディターのInsert Objectメニューへの追加
      - `User`グローバル変数は実行中のユーザーを指す

- [x] ModuleScript実装
      - 返したテーブルをロードする(値はキャッシュされ、2回目以降のrequireは同一の値を返す)
      - `require(file:ModuleScript)`で取得
      - モジュール本体でのwait()/yieldは不可(同期実行)

- [x] taskモジュール
  - [x] delay(sec, fn): sec秒後にfnを実行
  - [x] spawn(fn): fnを並行(コンカレンシー)に実行
  - [x] wait(sec): `wait()`のエイリアス(書き心地の優先)。引数省略時は次フレーム再開
  - delay/spawnで起動した関数内でもwait()が使える(エンジン管理タスク)

- [x] scriptはworkspace下になくても動作するようにする
      - System配下ならどこでも(System直下・Users/User配下・Folder内など)実行される
      - Workspace外のscriptでは`workspace`グローバルはアクティブWorkspaceを指す
- [!] scriptがUserによってworkspaceを切り替えられるごとに再起動する問題を修正
      - 誤認と判明。実体は「UserのToolスロット(ホットバー)がPlay/Stopで残留し、
        前回シーンの幽霊Toolが増殖/AddToolがサイレント失敗する」バグで、これを修正した
        (resetSystemForReloadでSlots/currentToolをクリア、AddTool失敗時に警告ログ)

- [ ] Terrainに上下左右前後のブラシを実装する(現状上からしかできないため)

- [x] 無限増殖バグの修正
    - ```
      [RCBN_WARN][Instance.cpp:59] setParent: Key collision for 'Users' in System. Renamed new child to 'Users1' to avoid overwriting existing instance.
      [RCBN_WARN][Instance.cpp:59] setParent: Key collision for 'Users1' in System. Renamed new child to 'Users11' to avoid overwriting existing instance.
      [RCBN_WARN][Instance.cpp:59] setParent: Key collision for 'Users11' in System. Renamed new child to 'Users111' to avoid overwriting existing instance.
      [RCBN_WARN][Instance.cpp:59] setParent: Key collision for 'Users111' in System. Renamed new child to 'Users1111' to avoid overwriting existing instance.
      ```
### 物理エンジン関係

- [x] Seatが勝手に回転して、-Z方向を向く問題の修正
- [x] Attachmentは、アセンブリ剛体によって座標がずれ、不安定になる
      これは想定とは異なるので、つねに所属するCubeの相対座標を維持するべき

- [ ] アクセス違反の可能性あり
    - "C:\Users\Ryarta\DeveloppingGames\AIMade\floppacars\grokf.yaml"で発生
    - ```cpp
      BaseCube::~BaseCube() {
          // RCBN_LOG("BaseCube Destructor: " << this->Name);
          if (actor) {
              // 重要：レイキャスト等での逆引きを無効化するため、まず userData をクリアする
              actor->userData = nullptr;

              // Physics 側で actor を参照している可能性があるため（Physics::cubes など）、
              // 基本的には Physics::update のクリーンアップループに任せるのが安全。
              // ただし、物理エンジン自体が存在しない場合（終了時など）は、ここで明示的に解放する。
              if (!lastWorkspace || !lastWorkspace->physicsEngine) {
                  actor->release();
                  actor = nullptr; /* 例外が発生しました: W32/0xC0000005
                  Unhandled exception at 0x0000017B698C0DA0 in Recubin.exe: 0xC0000005: Access violation executing location 0x0000017B698C0DA0.
                  */
              }
          }
      }
      ```

    - ```
      0000017b698c0da0() (不明なソース:0)
      Recubin.exe!BaseCube::~BaseCube() Line 156 (c:\Users\Ryarta\Documents\Recubin\src\Instances\BaseCube.cpp:156)
      Recubin.exe!Cube::`scalar deleting destructor'(unsigned int) (不明なソース:0)
      [Inline Frame] Recubin.exe!std::_Ref_count_base::_Decref() Line 1183 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1183)
      [Inline Frame] Recubin.exe!std::_Ptr_base<BaseCube>::_Decref() Line 1408 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1408)
      [Inline Frame] Recubin.exe!std::shared_ptr<BaseCube>::{dtor}() Line 1713 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1713)
      Recubin.exe!Humanoid::~Humanoid() (不明なソース:0)
      Recubin.exe!Humanoid::`scalar deleting destructor'(unsigned int) (不明なソース:0)
      [Inline Frame] Recubin.exe!std::_Ref_count_base::_Decref() Line 1183 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1183)
      [Inline Frame] Recubin.exe!std::_Ptr_base<Humanoid>::_Decref() Line 1408 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1408)
      [Inline Frame] Recubin.exe!std::shared_ptr<Humanoid>::{dtor}() Line 1713 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1713)
      [Inline Frame] Recubin.exe!std::shared_ptr<Humanoid>::operator=(std::shared_ptr<Humanoid> &&) Line 1728 (c:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory:1728)
      Recubin.exe!User::despawnCharacter() Line 477 (c:\Users\Ryarta\Documents\Recubin\src\Core\User.cpp:477)
      Recubin.exe!main(int argc, char * * argv) Line 517 (c:\Users\Ryarta\Documents\Recubin\src\main.cpp:517)
      [Inline Frame] Recubin.exe!invoke_main() Line 78 (d:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl:78)
      Recubin.exe!__scrt_common_main_seh() Line 288 (d:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl:288)
      kernel32.dll!00007ffa22a4e957() (不明なソース:0)
      ntdll.dll!00007ffa24327c1c() (不明なソース:0)
      ```

### レンダリング関連

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

### ネットワーク関係
- [x] Systemにboolチェックボックスで、UseNetwork(alpha)を追加
  - trueの場合:  LocalScript/Scriptが区別されて動作する
  - falseの場合: 区別されず、　一切のネットワーク通信を行わない

- [x] Usersクラスを追加
  - Userはここに配置される
  - UseNetworkがfalseの場合、Userの名前は"User"のままとする
  - パスは`System/Users`
  - (複数Peer分のUser生成/破棄はv2.0応用tierとして未実装。今回は1プロセス=1ローカルUserのまま)

- [~] 動的ホスト移行（Host Migration）型サーバー・クライアント通信基盤の作成(基盤tierのみ完了。詳細はprogress.md参照)
  - [x] ENetのソースコード（enet.h / .c）をプロジェクトに直接取り込んでビルドできるように構成
  - [x] 信頼性のあるデータ（チャット・イベント、ホスト移行シグナル等：RELIABLE）と高速なデータ（位置同期等：UNRELIABLE）のチャンネル分離
  - [ ] 各ノードの計算資源（CPUスコア・レイテンシ等）を定期的に計測し、現在のホストへ通知・集計する仕組みの実装
  - [ ] 現ホストの離脱（ログアウト）検知時、集計データから「一番計算資源の多いゲスト」を自動選出し、世界の統治権限（Serverステート）をシームレスに引き継ぐハンドシェイク処理の実装
  - [ ] ホストの動的交代（ロール変更）が発生した際に、各自のLuauスクリプト環境（Script / LocalScriptの有効・無効化）やピアIDの識別状態を破綻なくリアルタイムに変革・共有できるインターフェイスの用意

## あるかもしれない質問

- Q. なんでUserのキャラクターはPlayerなの？
- A. Userはあなたのことです。一方、Playerは道化(アバター)に過ぎません。
つまり…あなたとアバターは異なる分身として表現されます。

## Macについて
  Metalだけでも困難ですが、PhysXのサポートも薄く、
  将来的なVulkan移行、Mac対応ライブラリ痩せを考慮すると、
  Mac対応は今後しないことを決めました。

## 現在の問題
- None

## 使用中の技術
- C++
- OpenGL(GL/GLFW)
- Luau
- stb_image
- cgltf
- ImGui & ImGuizmo
- YAML(yaml-cpp)
- miniaudio
- PhysX
- Windows bat
- Python
- font-awesome
- recastnavigation

## 使用予定の技術
- DirectX(Windows最適化)
- Vulkan(Linuxなど)

  ### Luar言語
  - Rust(Luarコンパイラ)
  - この言語は現在試験的です。
  - 詳細は[このファイル](doc/LPL.md)をご覧ください。
  
---