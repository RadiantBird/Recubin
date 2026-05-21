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
1. ProxmityPromptを実装
  - キーボードのいずれかのキーを長押しする
  - Triggeredイベントを作成
  - 秒数を指定できるようにする
  - BillboardGuiでコア実装する

2. 既存実装の仕様違反を見つける

3. このエンジンに何が必要か考える（人間の仕事）

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