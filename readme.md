# Recubin -Powering imagination again-

## 概要
Roblox風のローカルソフトウェア開発をするためのエンジン。

## 目標
- Robloxの感覚でソフトウェア（ゲーム）を開発し、ローカルで公開できるようにしたい。
- Robloxでできなかったことを実現する(複数Workspaceとか特殊効果とか)。
- わかりやすいエディターを作る。直感的であれ。
- 物理で遊べるようにする

## 現時点の懸念
- なし

## Todoリスト
(!: 中止|?: 不明|x: 達成済み)
- [x] コードをきれいにして、保守しやすくする
- [x] バグ修正
- []Userが外部に依存しないようにリファクタ（Physicsだけ、windowは入力に必要なため要件等） 
  - [] イベントは消費するようにpublic関数で触れるように
  - [] イベントフラグはprivateに

- [] パッケージャーをテスト
- [] 必要なら修正

- [] コンソールのラテン文字の範囲を超えた文字が化ける問題を修正する(Windows日本語版で特有の問題)

- [] Humanoidの追加
  - CharacterSettingを廃止
  - UserからCharacter関連のプロパティを削除
  - ツリー:
    System/
      Workspace
      StarterCharacter/ ->この中にあるものが自動的にWorkspaceの中にクローンされる
        Humanoid
        Root
        (その他のCube)

そういえばWeldしたアセンブリ剛体をキャラクターにくっつけて帽子みたいなのを作ろうとしたら動かなくて失敗したな。
これも修正する必要がある。
あと、TextLabelとか、Buttonが動作してるのかをスクリプト書いてテストしないといけないね。

- [] Mac対応を開始する
  **Metalは今のところ使用しない**(難易度が高い)
  - レンダラーを抽象化する(OS依存をなくし、関数の実装部分で分岐を行う)...要するにインターフェイス
  - OpenGL4.1に準拠した実装を行う
  - ファイル操作のインターフェイス化

## 中断された作業
- なし

## 現在の問題
- コピペ実装が多い（構造化されていない）

## 使用中の技術
- C++
- OpenGL
- Luau
- stb_image
- ImGui & ImGuizmo
- YAML
- miniaudio
- PhysX
- Windows bat
- Python

## 使用予定の技術
- DirectX(Windows最適化)
- Vulkan(Linuxなど)

  ### Luar言語用
  - Rust(Luarコンパイラ)

## ブラックリスト
- Metal(あくまでOpenGLのパイプラインを最適化したうえでMacにも配布)
---