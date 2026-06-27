# Recubin -Powering imagination again-
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)](https://isocpp.org/)
[![Luau](https://img.shields.io/badge/Luau-Language-007ACC?logo=lua)](https://luau-lang.org/)
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

- [!] Userインスタンス下のBaseCube系が描画されている問題を修正 <-ただの勘違い!!!

- [] 太陽と月を追加
  - ただのSphereの派生クラス
  - しかし無限の距離を持つ(Skyboxと同様)
  - 角度だけを持つ(もしくは方向ベクトル)
  - 太陽の反対側に月
  - スカイボックスの内側(だいたい4000stud)あたりを公転する

- [x] 物理マテリアルを追加
   & BaseCubeに書き換えられるようにエディター実装
  - デフォルトはプラスチック 
  - プルダウンで選択できるようにする

- [] 位置の変更などが正しくコマンド履歴に保存されていないことによる、破滅的操作(間違えた->意図しない巻き戻し->混乱)の修正

- [] LiquidCubeを追加
  - BaseCubeを継承
  - 浮力が発生する
  - コリジョンは持たない
  - 拡散はしない
  - 色が設定可能
  - 浮力は大体の侵入面積で侵入しているBaseCubeに力を加える(重力の反対方向、+Y固定ではない)
  - シェーダーで表面が水の波のように動く(優先度は低い)

- [] 光源系インスタンス
  - SpotLight
    - Front方向に長さ分の光を投射
    - 円錐の角度を持つ(大きさ)
  - PointLight
    - 半径rの全方向に光を投射
  それぞれ光の強さを持つ
  今回でLightingの色も可変となる

- [x] UI要素にImageLabelとImageButtonを定義
  - DecalやTextureと同様
  - 画像を参照させる
  - 機能はText系インスタンスと同じ

- [x] ヒエラルキーでかっこ書きのクラス名ではなく、アイコンを表示するようにする
  - 名前の前にアイコンを表示
  - ImGuiで実装可能かは不明

- [] Mac対応を開始する
  - **Metalは今のところ使用しない**(難易度が高い)
  - レンダラーを抽象化する(OS依存をなくし、関数の実装部分で分岐を行う)...要するにインターフェイス
  - OpenGL4.1に準拠した実装を行う
  - ファイル操作のインターフェイス化

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