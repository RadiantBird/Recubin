#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Spatial.hpp>

class CommandHistory;

// ===================================================
//  PropertiesPanel  — 選択中インスタンスのプロパティ編集
// ===================================================
class PropertiesPanel : public EditorPanel {
public:
    Instance**     selectedInstance = nullptr;  // SceneHierarchyPanel::selectedInstance へのポインタ
    CommandHistory* m_history       = nullptr;

    PropertiesPanel();
    void onRender() override;
};
