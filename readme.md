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
- [x] シーンファイルをテストプレイ中に読み込もうとしたら、
     ポップアップありで終了するか確認とってから
     すぐシーンを切り替えられるようにする
- [x] 間違えてPositionを変更してしまった場合にCtrl+Zを押しても、
     dirty判定にはなっているものの何も反映されない問題の修正
     (ほかにもある可能性あり)
* [x] ドラッガーの実装
  * [x] Cubeサイズ＋軸制限で移動範囲を計算する
  * [x] ドラッグ軸ごとに不要軸を除去する処理を実装

    * [x] xzドラッグ時：yを無効化
    * [x] xyドラッグ時：zを無効化
    * [x] yzドラッグ時：xを無効化
    
* [x] 制限ボックス外に出ないようにクランプ処理を実装
* [x] ImGuizmoの移動モードの白い球が動いてしまうバグを修正する

### 物理エンジン関係
- [x] マテリアルが違うと溶接できない問題の修正(水上でテストした結果)
- [x] マテリアルがBaseCube系でクローン時に保存されていない(他もある可能性あり)

- [x] 浮力の改善
     現在の浮力はかかっている力が一点のベクトルだけなため、すぐに船などを作ると転覆する。
     これを改善するために力を面に分散させ、総和がアルキメデスの原理に等しくなるようにする。
     アセンブリ剛体の場合も同様な分散を行う。

- [x] 物理制約のビジュアライザーを実装(デバッグ用)
  - モーター
  - 溶接
  - アタッチメント(丸っぽく、後述)
  - 回転方向(モーター)
  - Viewメニュー「Physics Debug」で切替(デフォルトOFF)

- [x] アタッチメントを実装
  - 位置と向きを持つ(CFrame)
  - [!] 従来の物理制約はBody(旧名)に改名 → 改名は不要と判断しスコープ外に確定
  - Weldはそのまま(アタッチメントの意味なし)
  - モーターやロープはアタッチメントの位置で制約を生成する
    (Attachment0/Attachment1は任意。未設定なら従来の中点/キューブ中心)

- [x] Forceインスタンスを実装
  - 角力/ベクトル切り替え
  - 加算するか維持するか切り替えできる
  - デバッグ目的でビジュアライズ可能
  
### レンダリング関連

- [] Mac対応を開始する
  - **Metalは今のところ使用しない**(難易度が高い)
  - OpenGL4.1に準拠した実装を行う
  - ファイル操作のインターフェイス化

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない

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