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

- [ ] ModuleScript実装
      - 返したテーブルをロードする
      - `require(file:ModuleScript)`で取得

- [ ] taskモジュール
  - [ ] delay(sec, fn): sec秒後にfnを実行
  - [ ] spawn(fn): fnを並行(コンカレンシー)に実行
  - [ ] wait(sec): `wait()`のエイリアス(書き心地の優先)

- [ ] scriptはworkspace下になくても動作するようにする
- [ ] scriptがUserによってworkspaceを切り替えられるごとに再起動する問題を修正

- [ ] Terrainに上下左右前後のブラシを実装する(現状上からしかできないため)

### 物理エンジン関係
- [ ] ...

### レンダリング関連

- [x] Mac対応を開始する
  - **Metalは今のところ使用しない**(難易度が高い)
  - [x] OpenGL4.1に準拠した実装を行う(GLFWにCore Profile 4.1のコンテキストを明示指定)
  - [x] ファイル操作のインターフェイス化
  - [x] CMakeLists.txt/build.pyにMac向け分岐を追加(GLFW/GLEWはHomebrew、Luau/yaml-cppはFetchContentでソースビルド)
  - [x] Mac実機(Apple Silicon)でRecubinの起動・描画・Luau動作を確認
  - [x] PhysX 5.6.1をMac実機向けに自前ビルド(`TARGET_BUILD_PLATFORM=linux`として騙し、Apple Clang対応・ARM SIMDフォールバック等を局所パッチ。詳細は`progress.md`参照)し、Play modeでの物理動作を実機確認

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

### ネットワーク関係
- [ ] Systemにboolチェックボックスで、UseNetwork(alpha)を追加
  - trueの場合:  LocalScript/Scriptが区別されて動作する
  - falseの場合: 区別されず、　一切のネットワーク通信を行わない

- [ ] Usersクラスを追加
  - Userはここに配置される
  - UseNetworkがfalseの場合、Userの名前は"User"のままとする

- [ ] Windows（ホスト）とMac（ゲスト）間のP2P通信基盤の作成
  - ENetのソースコード（enet.h / .c）をプロジェクトに直接取り込んでビルドできるように構成
  - 信頼性のあるデータ（チャット・イベント等：RELIABLE）と高速なデータ（位置同期等：UNRELIABLE）のチャンネル分離
  - [ ] 起動時に「ホストとして待機」か「指定IPへ直接接続」を選択できるデバッグ用CUI/GUIの実装
  - [ ] WindowsとMac間で、構造体をそのまま送っても大丈夫なようにバイトオーダー（エンディアン）の吸収処理を実装
  - [ ] 接続が確立した際に、お互いのLuauのスクリプト環境でピアID（Client/Serverの識別）を共有できるインターフェイスの用意

## あるかもしれない質問

- Q. なんでUserのキャラクターはPlayerなの？
- A. Userはあなたのことです。一方、Playerは道化(アバター)に過ぎません。
つまり…あなたとアバターは異なる分身として表現されます。

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