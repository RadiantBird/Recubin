#pragma once
#include <Util/IPlatform.hpp>

// ==================================================================
//  MockPlatform
//
//  Mac対応準備用のスタブ実装。Windowsヘッダに一切依存しない。
//  ダイアログ系は常に空文字列(=キャンセル扱い)、その他はno-opを返す。
//  実際のMac実装(Finder/NSOpenPanel等)に差し替えるまでのプレースホルダー。
// ==================================================================
class MockPlatform : public IPlatform {
public:
    std::string openFileDialog(const std::vector<FileFilter>&) override;
    std::string saveFileDialog(const std::vector<FileFilter>&, const std::string&) override;
    std::string openFolderDialog() override;
    void revealInFileManager(const std::string&) override;
    ApplicationIconResult setApplicationIcon(const std::string&) override;
    void setupConsoleUtf8() override;
    void setupDllSearchPath() override;
    void* loadDynamicLibrary(const std::string&) override;
    void* getSymbol(void*, const std::string&) override;
    void freeDynamicLibrary(void*) override;
    std::unique_ptr<IChildProcess> launchChildProcess(
        const ChildProcessLaunchOptions& options) override;
};
