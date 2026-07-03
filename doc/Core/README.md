# Core

エンジンコアシステム群。各システムは独立したシングルトンまたは静的クラスとして動作し、`main.cpp` が初期化・フレームループ管理を行う。

| クラス | ファイル | 概要 |
|---|---|---|
| [Physics](Physics.md) | `include/Core/Physics.hpp` | PhysX 物理シミュレーション |
| [Renderer](Renderer.md) | `include/Core/Renderer.hpp` | OpenGL レンダリングパイプライン（GUI描画は `Renderer_GUI.cpp` に分割） |
| [LuauEngine](LuauEngine.md) | `include/Core/LuauEngine.hpp` | Luau スクリプティングランタイム（Dispatch/Math は `LuauEngine_Dispatch.cpp` / `LuauEngine_Math.cpp` に分割） |
| [AudioService](AudioService.md) | `include/Core/AudioService.hpp` | miniaudio 空間オーディオ |
| [SceneLoader](SceneLoader.md) | `include/Core/SceneLoader.hpp` | YAML シーンデシリアライズ |
| [SceneRuntime](SceneRuntime.md) | `include/Core/SceneRuntime.hpp` | シーンロード〜グローバル登録の起動手順の一括関数群 |
| [FileLoader](FileLoader.md) | `include/Core/FileLoader.hpp` | テキスト・バイナリ I/O |
| [User](User.md) | `include/Core/User.hpp` | カメラ・キャラクター制御 |
| [SystemState](SystemState.md) | `include/Core/SystemState.hpp` | グローバルエンジン状態フラグ |
| [GLFWInputBackend](GLFWInputBackend.md) | `include/Core/GLFWInputBackend.hpp` | GLFW 入力バックエンド（`IInputBackend` 実装） |
| [PropertyRegistry](PropertyRegistry.md) | `include/Core/PropertyRegistry.hpp` | プロパティ宣言の一元管理（Luau/YAML/clone/エディター駆動） |
| [RCBNScriptSignal](RCBNScriptSignal.md) | `include/Core/RCBNScriptSignal.hpp` | Luau 向けイベント（シグナル/コネクション）実装 |
| [Terrain](Terrain.md) | `include/Core/Terrain.hpp` | ボクセル地形 Instance（実データは TerrainStreamer） |
| [TerrainStreamer](TerrainStreamer.md) | `include/Core/TerrainStreamer.hpp` | 地形チャンクの非同期ストリーミング・生成・保存 |
| [LuarCompiler](LuarCompiler.md) | `include/Core/LuarCompiler.hpp` | `.luar`→Luau トランスパイラ DLL ラッパー |
| [Packager](Packager.md) | `include/Core/Packager.hpp` | ゲームの配布用パッケージ書き出し |
