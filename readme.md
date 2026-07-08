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

- [x] ヘッドレステスターを改善
  - [x] GL関係でクラッシュするのを修正
  - [x] GUIなしで操作できるようにする(CLI)
  - [x] 動作確認の自動化Pythonスクリプト
        - リグレッションテスト用

### エディター関係
- [ ] エディター自体のパッケージャー(デプロイ)の作成
  - Pythonで可
  - フォルダ
  - 根本的な依存ファイルを整理して移動させておく(現状は丸投げファイルが多い)
  - DLLコピー
  - exeコピー

- [x] さすがに「その他」にインスタンスが多すぎるので、分類を細分化する

- [ ] 英語/日本語混ざりをやめ、ローカライズテーブルを作成し、文字列を外部化する
      - **プロパティ欄は翻訳しない(英語固定)**
      - **クラス名も翻訳しない(同上)**
      - ホバーテキストなどは翻訳対象

- [ ] BaseResolutionは安全マージンでないのでそのカテゴリの線の上に出す

- [x] `[RCBN_WARN][Instance.cpp:45] setParent: Key collision for '        {Instance.Name}' in System. Overwriting existing child.`
      を修正する
- [x] キーコリジョンによるバグ/メモリリークを防止する(エラーにするのではなく、警告を出力してずらす)
      - 現状、ユーザー入力にバリテーションがない
- [x] システム配下がリセットされないことによるインスタンスの意図しない残存コピーができることの修正

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