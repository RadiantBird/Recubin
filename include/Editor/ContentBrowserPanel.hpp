#pragma once
#include <Editor/EditorPanel.hpp>
#include <string>
#include <vector>
#include <filesystem>

// ===================================================
//  ContentBrowserPanel  — assets/ ディレクトリのファイル一覧
// ===================================================
class ContentBrowserPanel : public EditorPanel {
public:
    ContentBrowserPanel();
    void onRender() override;

private:
    std::filesystem::path currentPath;
    void drawDirectory(const std::filesystem::path& path);
};
