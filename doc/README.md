# Recubin Engine — クラスドキュメント

C++23 製 3D ゲームエンジンエディタ。OpenGL レンダリング・PhysX 物理・Luau スクリプト・ImGui エディタ UI を統合。

---

## ディレクトリ構成

| ディレクトリ | 内容 |
|---|---|
| [Math/](Math/) | 数学基盤（Vector3, Quaternion, CFrame, Matrix4） |
| [Util/](Util/) | ユーティリティ（Color4, Material, Logger） |
| [Instances/](Instances/) | シーンオブジェクト階層（Instance ツリー） |
| [Core/](Core/) | エンジンコアシステム（Physics, Renderer, LuauEngine 等） |
| [Editor/](Editor/) | ImGui エディタ UI パネル群 |

---

## アーキテクチャ概要

```
main.cpp
  ├─ Renderer         ← 3D + ImGui 描画
  ├─ Physics          ← PhysX シミュレーション
  ├─ LuauEngine       ← スクリプト実行
  ├─ AudioService     ← 空間オーディオ
  ├─ User             ← カメラ・キャラクター制御
  └─ Workspace        ← シーンルート（Instance ツリー）
       ├─ Cube (BaseCube → Spatial → Instance)
       ├─ Script
       ├─ Sound
       ├─ Model
       └─ Decal
```

## 依存関係グラフ（外部ライブラリ）

| ライブラリ | 用途 |
|---|---|
| GLFW / GLEW | OpenGL コンテキスト・入力 |
| ImGui + ImGuizmo | エディタ UI・ギズモ操作 |
| PhysX | 剛体物理シミュレーション |
| Luau | スクリプティング VM |
| miniaudio | オーディオ再生 |
| YAML-cpp | シーンファイルの読み書き |
| stb_image | テクスチャ画像読み込み |

## フレームループ概要

```
毎フレーム:
  1. SystemState 同期 (isPlaying / isPaused)
  2. Play/Stop トランジション処理
  3. [Play 中] physics->update()
  4. [Play 中] luauEngine->update() → スクリプト再開
  5. [Play 中] luauEngine->executeWorkspaceScripts()
  6. user->processInput()
  7. renderer->render() → 3D シーン + ImGui
  8. audioService->updateSounds()
```
