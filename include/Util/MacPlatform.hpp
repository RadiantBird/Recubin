#pragma once
#include <Util/IPlatform.hpp>

// macOS向けIPlatform実装。Cocoaファイルダイアログ・NSWorkspace・
// POSIX動的ライブラリAPIをOS依存処理として集約する。
class MacPlatform : public IPlatform {
public:
    std::string openFileDialog(const std::vector<FileFilter>& filters) override;
    std::string saveFileDialog(const std::vector<FileFilter>& filters, const std::string& defaultExt) override;
    std::string openFolderDialog() override;
    void revealInFileManager(const std::string& path) override;
    ApplicationIconResult setApplicationIcon(const std::string& path) override;
    void setupConsoleUtf8() override;
    void setupDllSearchPath() override;
    void* loadDynamicLibrary(const std::string& name) override;
    void* getSymbol(void* handle, const std::string& symbolName) override;
    void freeDynamicLibrary(void* handle) override;
    std::unique_ptr<IChildProcess> launchChildProcess(
        const ChildProcessLaunchOptions& options) override;
};
