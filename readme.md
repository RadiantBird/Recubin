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
- [!]**Userが外部に依存しないようにリファクタ（Physicsだけ、windowは入力に必要なので却下）** 
  - [] イベントは消費するようにpublic関数で触れるように
  - [] イベントフラグはprivateに
  - 価値を感じないので今は保留で(スタイルもわからん)

- [] 特殊効果を追加(ブラウン管とか色彩補正とかポスタリゼーションとかピクセル化とか)
- [x] Tool(ツール)インスタンスを追加
  - ツリー:
    (先頭にドットがあったらプロパティ)
    User
      Inventory
        Item1
        Item2
        ...
      .Slots = [&Item2, nullptr, &Item1, ...]  // Inventoryの子への参照
      .currentTool

    Workspace
      Character
        (この階層にcurrentToolが移動してくる)
        

  - User.Slots(array)の1~9+0(10とみなす)を配置
  - フロー:
      入力:
        1. Userが入力を取得(マウス・キーボード)
        - 左・真ん中・右クリックを取得
        - {1~9, 0}の数字キーを取得
      Tool選択:
        1. 入力されたキーを整数に変換 -> `int i` (ただしキー0 -> 10)
        2. `array[i]`のToolインスタンスをUser.currentToolにポインタを設定
        3. currentTool.Parent = Characterにしておく
        4. arrayに配置してあるcurrentToolは選択済みとして記録する
      Tool選択解除/変更:
        1. currentToolの選択済みを解除
        2. currentToolのポインタがあったらnullptrにする
        3. currentTool.Parent = Inventory
        4. さっきとほかのキー -> Tool選択
      左クリックされたとき:
        1. currentToolがあるかチェック
        2. currentToolのActivatedイベントを発火
      Toolが装備されている間:
        1. Handleプロパティに指定されたCubeは右手の端っこに移動(毎フレームごと)
  
  - インベントリもインスタンスとして定義（とりあえずフォルダでよいのでは）
  - 同じ名前のツールをインベントリに複数個入れようとすると名前が変わるのは仕様とする(unordered_mapとして作られているため)
- [] Toolは両手で持てるようにカスタマイズ

- [] Terrainインスタンスを追加
  - [] 角柱(直角三角形の底面を持つものを追加)と立方体で構成される
  - [x] ノイズを用いて自動生成
  - [x] 4x4x4 studsのサイズを想定
  - [?] 原点はブロック座標が整数になるように調整して決定(あとでどこが適切か考えておく)
  - [x] 物理と描画は、いちばん処理が軽くなるであろう方法で実行(いちいちすべてをCubeインスタンスにしない)
  - [] 部分的な置き換えや削除をできるようにする（C++/Luau APIの用意）
  - ブラシ機能（直感的に地形を盛ったり削ったり滑らかにしたり）は難しそうなので、実装方針が固まるまで保留

- [] パッケージャーをテスト
- [] 必要なら修正

## 中断された作業
- なし

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
  - Rust(Luarコンパイラ)

## ブラックリスト
- Metal(Macには配布しません。(OpenGLを廃止したことを思い知れ) )
---