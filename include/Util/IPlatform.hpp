#pragma once
#include <string>
#include <vector>

// ==================================================================
//  IPlatform
//
//  OS依存の操作(ファイル/フォルダダイアログ、ファイルマネージャーで開く、
//  コンソールのUTF8設定、動的ライブラリのロード)を抽象化するインターフェイス。
//  呼び出し側はこのIF経由でのみOS機能を使い、特定OSのAPIに直接依存しない。
//  将来Mac向けの実装に差し替えられるようにする(IInputBackendと同じ設計方針)。
// ==================================================================
struct FileFilter {
    std::string name; // ダイアログのファイル種別欄に表示される名前 (例: "Scene (*.yaml;*.yml)")
    std::string spec; // セミコロン区切りの拡張子パターン (例: "*.yaml;*.yml")
};

enum class ApplicationIconResult {
    Unsupported,
    Applied,
    Failed,
};

class IPlatform {
public:
    virtual ~IPlatform() = default;

    // ファイルを開くダイアログ。キャンセル/失敗時は空文字列を返す。
    virtual std::string openFileDialog(const std::vector<FileFilter>& filters) = 0;

    // ファイルを保存するダイアログ。defaultExtは拡張子未入力時に補う既定の拡張子(例: "yaml")。
    virtual std::string saveFileDialog(const std::vector<FileFilter>& filters,
                                        const std::string& defaultExt) = 0;

    // フォルダ選択ダイアログ。
    virtual std::string openFolderDialog() = 0;

    // OS標準のファイルマネージャー(エクスプローラー/Finder相当)でパスを開く
    virtual void revealInFileManager(const std::string& path) = 0;

    // OS固有のアプリケーションアイコンを設定する。空パスは既定アイコンへの復帰を表す。
    // ウィンドウ単位のアイコンしか持たないOSはUnsupportedを返し、呼び出し側で処理する。
    virtual ApplicationIconResult setApplicationIcon(const std::string& path) = 0;

    // 起動時に1回呼ぶ、コンソールの入出力コードページをUTF-8にする処理
    virtual void setupConsoleUtf8() = 0;

    // 起動時に1回呼ぶ、実行ファイルと同階層のdllsをDLL検索パスに追加する処理
    virtual void setupDllSearchPath() = 0;

    // 動的ライブラリのロード(luar_compiler.dll等)。ハンドルはvoid*で抽象化。
    virtual void* loadDynamicLibrary(const std::string& name) = 0;
    virtual void* getSymbol(void* handle, const std::string& symbolName) = 0;
    virtual void  freeDynamicLibrary(void* handle) = 0;
};
