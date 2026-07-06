#pragma once

class IPlatform;

// 現在のプロセスで使うIPlatform実装への参照を返す(Renderer::instanceと同様のシングルトンアクセサ)。
// 通常はWindowsPlatformを返すが、環境変数RECUBIN_MOCK_PLATFORMが設定されている場合は
// MockPlatformを返す(Macビルド環境が無い状態でMock切り替えを検証するためのスイッチ)。
IPlatform& getPlatform();
