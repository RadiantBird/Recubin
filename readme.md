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
(!: 中止|?: 不明|x: 達成済み)

- [x] ナビメッシュパスファインディング
  - Vector3の座標配列を返す
  - 独立したサービス
  - Humanoidから動かしてテストしてみる
  - ジャンプも考慮したパスを算出する

- [] Humanoid改修
  - JumpHeight(stud)を実装(自動でJumpPowerも計算される)

- [x] Scriptにメソッド/プロパティをバインディング
  - Enabled
  - Aborted(readOnly)
  - Source(readOnly)
  - Restart(スクリプトを再起動する->特殊再起パターンやウォッチドッグに活用できる)

- [x] 安全対策の組み込み
  - 1フレームでCloneしすぎたらランタイムを強制停止させ、Cloneを呼び出していたスクリプトの種類で大きい順にソートしてエラー出力(Infinite cloning possible)
  - Restartしすぎも同様(Infinity recursion possible)
  - その他、スクリプトがループ処理で時間がかかりすぎたらタイムアウトさせる
  - これらの安全マージン変数はSystemクラスに埋め込み、エディターで編集可能にする(Luauバインドは意図的に行わない)

- [x] カメラをプログラマブルに
  - LuauからUserを通して使えるようにする
  - ControlMode::Programを追加
  - 簡単なテストスクリプトを作成し、意図通りにカメラのCFrameが反映か手動テスト

- [x] PostEffectにシェーダーを増やす
  - Saturation(-1: 反転, 0:白黒, 1 >=:ラップアラウンド)
  - VHS(デルタタイムでノイズを発生させるだけ)
  - ChromaticAberration(CA) (0~1でチャンネルのずれ方を変える)

- [x] アンチパターン修正
  - Instance.cppの例のあのstatic_assert
    - 物理制約が適用されるのはBaseCubeからのはずなので、そっちに実装を移す

- [] Mac対応を開始する
  - **Metalは今のところ使用しない**(難易度が高い)
  - レンダラーを抽象化する(OS依存をなくし、関数の実装部分で分岐を行う)...要するにインターフェイス
  - OpenGL4.1に準拠した実装を行う
  - ファイル操作のインターフェイス化

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