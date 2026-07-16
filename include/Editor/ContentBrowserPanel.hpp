#pragma once
#include <Editor/EditorPanel.hpp>
#include <string>
#include <vector>
#include <filesystem>

class Instance;
class Workspace;
class CommandHistory;

// ===================================================
//  ContentBrowserPanel  — assets/ ディレクトリのファイル一覧
// ===================================================
class ContentBrowserPanel : public EditorPanel {
public:
    // EditorManager が配線する（SceneHierarchyPanel と選択・ワークスペースを共有）
    Instance**      selectedInstance = nullptr;
    Workspace**     workspace        = nullptr;
    CommandHistory* m_history        = nullptr;

    ContentBrowserPanel();
    void onRender() override;

private:
    std::filesystem::path currentPath;
    void drawDirectory(const std::filesystem::path& path);
};
