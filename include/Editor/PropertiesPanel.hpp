#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Spatial.hpp>

// ===================================================
//  PropertiesPanel  — 選択中インスタンスのプロパティ編集
// ===================================================
class PropertiesPanel : public EditorPanel {
public:
    Instance** selectedInstance = nullptr;  // SceneHierarchyPanel::selectedInstance へのポインタ

    PropertiesPanel();
    void onRender() override;
};
