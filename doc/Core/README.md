# Core

エンジンコアシステム群。各システムは独立したシングルトンまたは静的クラスとして動作し、`main.cpp` が初期化・フレームループ管理を行う。

| クラス | ファイル | 概要 |
|---|---|---|
| [Physics](Physics.md) | `include/Core/Physics.hpp` | PhysX 物理シミュレーション |
| [Renderer](Renderer.md) | `include/Core/Renderer.hpp` | OpenGL レンダリングパイプライン |
| [LuauEngine](LuauEngine.md) | `include/Core/LuauEngine.hpp` | Luau スクリプティングランタイム |
| [AudioService](AudioService.md) | `include/Core/AudioService.hpp` | miniaudio 空間オーディオ |
| [SceneLoader](SceneLoader.md) | `include/Core/SceneLoader.hpp` | YAML シーンデシリアライズ |
| [FileLoader](FileLoader.md) | `include/Core/FileLoader.hpp` | テキスト・バイナリ I/O |
| [User](User.md) | `include/Core/User.hpp` | カメラ・キャラクター制御 |
| [SystemState](SystemState.md) | `include/Core/SystemState.hpp` | グローバルエンジン状態フラグ |
