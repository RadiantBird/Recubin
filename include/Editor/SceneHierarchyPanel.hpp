#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Instance.hpp>
#include <string>

// ===================================================
//  SceneHierarchyPanel  — ワークスペースのインスタンスツリーを表示
// ===================================================
class SceneHierarchyPanel : public EditorPanel {
public:
    Workspace*  workspace       = nullptr;
    Instance*   selectedInstance = nullptr;  // PropertiesPanel と共有

    SceneHierarchyPanel();
    void onRender() override;

private:
    void drawNode(Instance* inst);
};
