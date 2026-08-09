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
    std::string openFileDialog(const std::vector<FileFilter>&) override { return {}; }
    std::string saveFileDialog(const std::vector<FileFilter>&, const std::string&) override { return {}; }
    std::string openFolderDialog() override { return {}; }
    void revealInFileManager(const std::string&) override {}
    ApplicationIconResult setApplicationIcon(const std::string&) override {
        return ApplicationIconResult::Unsupported;
    }
    void setupConsoleUtf8() override {}
    void setupDllSearchPath() override {}
    void* loadDynamicLibrary(const std::string&) override { return nullptr; }
    void* getSymbol(void*, const std::string&) override { return nullptr; }
    void  freeDynamicLibrary(void*) override {}
};
