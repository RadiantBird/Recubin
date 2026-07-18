# Recubin -Powering imagination again-
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)](https://isocpp.org/)
[![Luau](https://img.shields.io/badge/Luau-Language-007ACC?logo=luau)](https://luau-lang.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.1-E65226?logo=opengl)](https://www.opengl.org/)

![logo](Recubin.png)

RecubinはC++で作成されたゲームエンジンで、
楽しい物理エンジン、Luauでコーディング、複数Workspaceなど、
ゲーム開発を楽しくしてくれる要素がたくさんです。
Windowsで動作します。
Linux対応予定です。

## 目標
- 誰でも簡単にゲームを開発し、ローカルで公開できるように。
- Robloxでできなかったことを実現する。
- わかりやすいエディターを作る。直感的であるように。
- 物理で遊べるようにする。

## Todoリスト
(!: 中止|?: 不明|~: 保留中|x: 達成済み)

### 設計関係
- N/A

### エディター関係
- [] プレイヤーキャラクターをずっと固定にするのではなく、User.Characterとして
参照を可視化し、Luauで動的に変更できるようにする

- [x] リグビルダーで生成したモデルを歩かせるとかなり転倒しそうになっており、
加えてPlayを押したときに謎にRootが(0,?,0)に戻るようになっている。
これを修正する。
追記: PathfinderTestのスクリプトには、Rootの強制移動等は含まれていない模様。

### 物理エンジン関係

- N/A

### レンダリング関連

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

### ネットワーク関係

- [~] NetworkEvent

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
- xatlas

## 使用予定の技術
- DirectX(Windows最適化)
- Vulkan(Linuxなど)

  ### Luar言語
  - Rust(Luarコンパイラ)
  - この言語は現在試験的です。
  - 詳細は[このファイル](doc/LPL.md)をご覧ください。
  
---