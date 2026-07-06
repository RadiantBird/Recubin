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

- [x] パーティクルを実装
  - 火
  - 煙
  - 水しぶき
  - 正方形がぶっ飛ぶやつ(汎用的な)
  - 色、サイズ、一度に出す多さ、移動スピードなどを定義
  - 重力に影響される(火と煙は反対方向に(つまり上昇する))
  - テクスチャなしの数式描画でまず実装

- [x] 天気システムを追加
  - 雲
    - 3Dノイズ関数(Perlin/Simplex)の数式描画
    - CloudCover (0.0〜1.0: 雲の量)
    - CloudDensity (0.0〜1.0: 雲の濃さ・不透明度)
    - WindDirection (Vector3: 雲が流れる風の向きと速さ)
  - 雨(パーティクルを流用し、衝突判定で遮る)
  - 雪(テクスチャ違うだけ)
  - 風(火と煙の向きに作用するが、風が当たるかの判定はなし)
  - 雷(面白そう、金属製のCubeで、高いもののところほど落下確率が上がる)
  - アンビエント音源(差し替え可能)
  - 現在の天候はLuauで取得可能
  - これらの要素は天気システム(Weatherクラス)として統合

- [] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

- [x] 透明な画像をDecal/Textureとして貼るとそのCube自体が透ける問題の修正

- [x] ImageLabel/ImageButtonがSurfaceGui/BillboardGuiに未対応な問題の修正
  - [~] IsAだと不自然なのでHasAにリファクタ

- [x] コピペしたときにCube1->Cube11みたいにナンバリングがおかしくなる問題の修正
- [x] 選択中にF2ですぐにインスタンスの名前を変更できるようにする

- [] OpenGLパイプラインを最適化(必要かは検討の余地あり)

- [] Mac対応を開始する
  - **Metalは今のところ使用しない**(難易度が高い)
  - レンダラーを抽象化する(OS依存をなくし、関数の実装部分で分岐を行う)...要するにインターフェイス
  - OpenGL4.1に準拠した実装を行う
  - ファイル操作のインターフェイス化

- [x] viewportFocusedでないのにマウスをスクロールするとカメラがズームしたりするのを修正

- [x] キャラクターQoL改善
  - 水中でもジャンプできる
  - はしご(Truss)っていうBaseCubeの派生クラスを追加
    - 垂直方向に登れるようになる

- [x] Seatクラスの追加
  - 接触するとハードコードの座った状態のポーズに移行し、RootをSeatと溶接
  - ジャンプで溶接を解除
  - Seatと接続中、Steer(A:-1, D:1, Null:0)とThrottle(W:1,S:-1,Null:0)情報を更新し続ける(入力状況が更新されたとき)
  - ポージング角度の状態を決めるフラグが多くなってきたので三項演算ではなくif else推奨

- [x] 簡単なScreenGuiフォーマット機能
  - Systemに基準解像度を定義(1920x1080)など
  - その画面の基準解像度とGUI要素のX,Yそれぞれの比率を計算
  - 開発者は何も考える必要がない
  - 実際の画面のレンダリングは、基準解像度に従って適切にスケーリングされる
  - この機能は揃えの基準をPixelにしてるときに有効

- [x] 風の向きと雲のスクロール方向が逆な問題を修正

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


## ブラックリスト
- Metal(あくまでOpenGLのパイプラインを最適化したうえでMacにも配布)
---