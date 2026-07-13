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

- [ ] ツールバー拡張
  - [ ] 今まで通り、{再生|選択、移動、...、保存読み込み}の基本バー
  - [ ] {Cube, Cylinder, ... Liquid,...}のオブジェクトバー(オブジェクトを追加ボタンは廃止)
  - [ ] 地形操作用のバー（プロパティ欄から移動）
  - [ ] 物理関係 {物理制約ならべる}バー
  - [ ] キャラクター関係
    - [ ] Humanoid追加ボタン(インスタンスが選択されてるとき)
    - [ ] リグビルダーボタン(デフォルトキャラクターをModelとしてそのまま配置する)

- [ ] アタッチメントの位置移動などがコマンド履歴に保存されていない問題の修正

- [ ] Particleインスタンス、Weatherインスタンスの保存されるべきプロパティが保存されていない問題の修正

- [ ] Terrainに上下左右前後のブラシを実装する(現状上からしかできないため)

- [ ] アニメーションの自動ループをやめ、ループするかどうかを設定できるようにする（エディター、スクリプト両方）
- [ ] キーフレームに到達した、というシグナルイベントを追加
- [ ] SignalEventインスタンスを追加
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
- NoCollisionインスタンス
  - BaseCube A/B間の衝突をなくす
  - それ以外とは引き続き衝突する

### レンダリング関連

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

- [x] カスタムメッシュにUVを自動生成したい
    - これが実現すればどこでもデカールやテクスチャーが貼れるようになる

- [x] Canvasインスタンスを追加
    - Decalに似ている
    - 任意の大きさを生成(SurfaceGuiのような感じ)
    - ドットを打ったりできる
    - 並行してレイキャストして当たった三次元座標が、その表面のどこにあたるかの計算方法も考えたい
    - [x] Tool使用->レイキャスト発射->表面のUV座標にペイント、というデモシーンを作成しテスト
      - そういえば画面のマウスの位置から3D空間にレイキャスト、という仕組みがLuau側にないような。組み込む必要がありそう
      - `User:GetMouseRay()`として実装済み
      - デモシーン: `assets/scenes/canvas_test.yaml`（`scripts/CanvasPaint.luau`）を作成済み。実機での動作確認は未実施（確認待ち）

- [x] Highlightインスタンスを追加
    - 深度無効化
    - 単色塗り
    - 外側は縁取り

    - エディターで選択したときに出てくるワイヤーフレームもこれに変更していく

- [x] BaseCubeはアルファが半透明の範囲にあるとき、近くのものを透明化して描画しなくしてしまう問題の修正

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

## 使用予定の技術
- DirectX(Windows最適化)
- Vulkan(Linuxなど)

  ### Luar言語
  - Rust(Luarコンパイラ)
  - この言語は現在試験的です。
  - 詳細は[このファイル](doc/LPL.md)をご覧ください。
  
---