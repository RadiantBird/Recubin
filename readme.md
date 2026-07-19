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
- [x] プレイヤーキャラクターをずっと固定にするのではなく、User.Characterとして
参照を可視化し、Luauで動的に変更できるようにする

- [x] リグビルダーで生成したモデルを歩かせるとかなり転倒しそうになっており、
加えてPlayを押したときに謎にRootが(0,?,0)に戻るようになっている。
これを修正する。
追記: PathfinderTestのスクリプトには、Rootの強制移動等は含まれていない模様。

### 物理エンジン関係

- []Trussインスタンスで、くっついているかどうかの判定が厳しいなどが理由で、トラスにくっついてる状態でキー操作をやめると、落下してしまう問題を修正し、その場に留まるようにする（回帰バグ？）

### レンダリング関連

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない
  - Q. これはOpenGL-4.1で可能？

- [x] 影が原点を離れると描画されなくなる問題の修正

### ネットワーク関係

- [x] GLFWなどのエラーで落ちないのに画面が真っ黒、何が起きているか不明
  - カメラの座標が飛んでいる？
  - ネットワークのオブジェクト同期はまだ実装されていないかもしれない
  - アセットエラーはなし
  - まずはウィンドウを描画し、そのあとにオブジェクトの座標を同期したりする機能を実装する
  - 解決(2026-07-19): 黒画面の原因はRenderer::render()の`if (!editor)`ゲートがランタイムのNullEditorManager(非null)でシーン描画をスキップする回帰。IEditorManager::ownsSceneRender()で修正・実機確認済み

- [x] オブジェクトの座標同期（ワールドレプリケーション）の実装(2026-07-19、ReplicationManager。詳細はprogress.md)
  - [x] アバター同期: 各ピアの自キャラ姿勢をHost経由で20Hz配布し、RemotePlayer_<id>として表示(衝突なしの純視覚、User.Characterとは独立)
  - [x] ワールド同期: Host権威。非AnchoredオブジェクトにnetIdを採番して配布(WorldMapping=RELIABLE / WorldTransforms=UNRELIABLE 20Hz)。クライアント側は対象をAnchored化(キネマティック)して受信cframeを平滑適用
  - [x] ホスト移行対応: 昇格時にAnchoredを自動復元して自分の世界を権威として再配布(localhost 3ピア+2段移行でログ検証済み)
  - 未対応(将来): 歩行アニメ同期、クライアントからの物理干渉(入力転送/所有権移譲)、非アクティブWorkspaceの同期


- [~] NetworkEvent

- [x] 動的ホスト移行（Host Migration）型サーバー・クライアント通信基盤の作成(応用tierまで完了(2026-07-19)。詳細はprogress.md参照(なかったらdoc/archive.md)。LAN/localhost前提: NAT越えは未対応。ワールドレプリケーションは2026-07-19に実装済み(上記参照))
  - [x] ENetのソースコード（enet.h / .c）をプロジェクトに直接取り込んでビルドできるように構成
  - [x] 信頼性のあるデータ（チャット・イベント、ホスト移行シグナル等：RELIABLE）と高速なデータ（位置同期等：UNRELIABLE）のチャンネル分離
  - [x] 各ノードの計算資源（CPUスコア・レイテンシ等）を定期的に計測し、現在のホストへ通知・集計する仕組みの実装
  - [x] 現ホストの離脱（ログアウト）検知時、集計データから「一番計算資源の多いゲスト」を自動選出し、世界の統治権限（Serverステート）をシームレスに引き継ぐハンドシェイク処理の実装
  - [x] ホストの動的交代（ロール変更）が発生した際に、各自のLuauスクリプト環境（Script / LocalScriptの有効・無効化）やピアIDの識別状態を破綻なくリアルタイムに変革・共有できるインターフェイスの用意（System.NetworkRoleChanged / System.NetworkRole / System.LocalPeerId）

### その他

- [ ] ツールを持っていないほうの手がなぜか上を向く問題の修正
  - User.cpp 251行目 TODO

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