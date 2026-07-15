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

- Vector3とQuartanionは、CFrameによって一発で生成(変換)できるようにならないだろうか(エディター、サンプルスクリプトを見て思った)

### エディター関係
- [ ] エディター自体のパッケージャー(デプロイ)の作成
  - Pythonで可
  - フォルダ
  - 根本的な依存ファイルを整理して移動させておく(現状は丸投げファイルが多い)
  - DLLコピー
  - exeコピー
  - imgui.iniコピー
  - リリースビルドテストのためにzipまで自動化できると理想的

- [x] ギズモ改良
  - [x] Ctrl+Lを押すとローカル/ワールド軸に切り替わるようにする(Cubeを中心とした、XYZ軸がローカル)
  - [x] Tabを押すと、押したときのマウスの位置にギズモが移動してくる(大きいものの移動に便利)
  - [x] 選択モードのときにCtrlと左クリックすると複数選択できるようにする
  - [x] 複数選択したときのギズモの位置を、集合の中心にする
  - [x] モデルを選択した状態でただのCubeと同じ感覚で移動できるようにする。バウンディングボックスなどを用いる
  - [x] ビューポートでCubeを選択した状態でCtrl+Fを押すと、エクスプローラーが自動的にその位置まで展開され、すぐに見えるようにする

- [x] プロパティ編集改良
  - 複数選択時に、共通したプロパティ名をまとめて表示する
  - 編集すると複数にその値が通る
  - 値が曖昧になっている場合は`(不明)`/`(Unknown)`と出力することとする

- [x] 衝動的にボタンを押さないように、危険操作には3秒のクールタイムを設ける(地形再生成/セーブするかしないか)
  - 大半の場合、セーブしてからエディターを閉じるはずですが、不正終了しようとしてるということは動揺している可能性が高いと推測したため。
  - ボタンも赤くする（反対に安全操作は緑）

- [x] 本当に実装できているか、Luauスクリプトとシーンファイルでテストする
  - [x] キーフレームに到達した、というシグナルイベントを追加
  - [x] SignalEventインスタンスを追加
    - [x] 確認したところ、エディターでインスタンスとして生成する方法がないので、対応する

    - SiEv:Fire(...)
- [~] NetworkEvent

- [ ] 値系インスタンスを追加
  - テンプレートで生成(可能なら)
  - ValueBase
    - IntValue
    - BoolValue
    - CFrameValue
    - Vector3Value
    - Color4Value
    - BoolValue
    - ObjectValue
    - QuartanionValue

### 物理エンジン関係

- BallSocketインスタンス
  - 360度自由回転
  - でも離れることも近づくこともない
- NoCollisionインスタンス
  - BaseCube A/B間の衝突をなくす
  - それ以外とは引き続き衝突する
  - Attachment引数は不要なので取らない

### レンダリング関連

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

### ネットワーク関係

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