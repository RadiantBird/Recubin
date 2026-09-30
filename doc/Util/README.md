# Util

汎用ユーティリティクラス群。

| クラス | ファイル | 概要 |
|---|---|---|
| [Color4](Color4.md) | `include/Util/Color4.hpp` | RGBA カラー（0〜1 float） |
| [Material](Material.md) | `include/Util/Material.hpp` | 物理マテリアルプロパティ |
| [Logger](Logger.md) | `include/Util/Logger.hpp` | マクロベースデバッグログ |
| [AssetGuard](AssetGuard.md) | `include/Util/AssetGuard.hpp` | ランタイム用アセットパストラバーサル防御 |
| [RuntimeFileSystem](RuntimeFileSystem.md) | `include/Util/RuntimeFileSystem.hpp` | 実行時ファイルI/O。Luau `IO.*`とTextFile保存の窓口（ポータブルroot・アトミック書き込み） |
| [IPlatform](IPlatform.md) | `include/Util/IPlatform.hpp` | OS依存操作(ファイルダイアログ/コンソール設定/DLLロード)の抽象化。実装は`WindowsPlatform`/`MockPlatform` |
