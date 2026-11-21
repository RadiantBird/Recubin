#pragma once
#include <string>
#include <filesystem>

// アセットのパストラバーサル防御。
// 配布ゲームを再生するランタイム(RecubinEngine.exe)でのみ有効化し、
// アセットルート(ゲームフォルダ)の外を指すパスの読み込みを拒否する。
// エディター(Recubin.exe)は enableSandbox を呼ばないため allow は常に true。
namespace AssetGuard {
    // サンドボックスを有効化し、アセットルートを確定する（ランタイム起動時に一度だけ呼ぶ）。
    void enableSandbox(const std::filesystem::path& root);

    // path がアセットルート配下なら true。無効時は常に true。
    // ルート外(絶対パス/`..`脱出/別ドライブ)は false を返し警告ログを出す。
    bool allow(const std::string& path);
}
