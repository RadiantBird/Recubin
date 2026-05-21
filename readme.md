# Recubin -Powering imagination again-

## 目標
- Robloxの感覚でソフトウェア（ゲーム）を開発し、ローカルで公開できるようにしたい。
- Robloxでできなかったことを実現する(複数Workspaceとか特殊効果とか)。
- わかりやすいエディターを作る。直感的であれ。
- 面白い作品を規制なしで作れるコミュニティを作る。ぼちぼち。
- 物理で遊べるようにする

## 現時点の懸念
- ない :D

## Todoリスト
(小さい数字から順番にやる)
1. Folderインスタンスを追加
  - 特別な機能は特になし
  - 単なる整理用インスタンス

2. ModelインスタンスをInsert Object欄に追加

3. 複数Workspaceを実装
  - System直下にいろいろなWorkspaceが存在し、演算できることが目標
  - Pキーでキャラクターはworkspace間を移動
  - レンダラーはキャラクターが存在するworkspaceを描画
  - カメラがフリーモードな場合、現在のworkspaceを描画し続ける
  - エクスプローラーでWorkspaceを右クリックすると、新しくボタンを追加する
    - 新しいビューポートで開く
    - このworkspaceに切り替える(レンダラーの描画対象を即座に切り替える)
  - scriptのworkspace変数は、所属しているworkspaceに基づく
  - "system"というルートインスタンスのグローバル変数を追加する
  - workspaceに以下のプロパティを追加する
    - PhysicsEnabled -> boolean
    - Gravity -> Vector3


## 中断された作業
- 特にない

## 現在の問題
- なし
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
  - node.js
  - TypeScript
  - Rust(コンパイラ最適化)

## ブラックリスト
- Metal(Macには配布しません。(OpenGLを廃止したことを思い知れ))
---