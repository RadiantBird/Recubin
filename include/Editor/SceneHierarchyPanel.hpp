#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Instance.hpp>
#include <memory>
#include <string>

class CommandHistory;

// ===================================================
//  SceneHierarchyPanel  — ワークスペースのインスタンスツリーを表示
// ===================================================
class SceneHierarchyPanel : public EditorPanel {
public:
    Workspace*  workspace        = nullptr;
    Instance*   selectedInstance = nullptr;  // PropertiesPanel と共有

    CommandHistory*              m_history   = nullptr;
    std::shared_ptr<Instance>*   m_clipboard = nullptr;  // EditorManager::m_clipboard へのポインタ

    SceneHierarchyPanel();
    void onRender() override;

private:
    // Script追加ダイアログ用
    std::shared_ptr<Instance> m_pendingScriptParent;
    bool                      m_openScriptDialog = false;

    void drawNode(Instance* inst);
    void renderNewScriptDialog();
};
