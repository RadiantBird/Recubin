#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>
#include <functional>
#include <memory>
#include <string>

class CommandHistory;

// キューブ指定ピッカーの共有状態（EditorManager / PropertiesPanel / ViewportPanel で共有）
struct PickerState {
    bool        active     = false;
    std::string prop;                                              // "Cube0" or "Cube1"
    Instance*   constraint = nullptr;
    std::function<void(std::shared_ptr<BaseCube>)> onPick;
};

// ===================================================
//  PropertiesPanel  — 選択中インスタンスのプロパティ編集
// ===================================================
class PropertiesPanel : public EditorPanel {
public:
    Instance**     selectedInstance = nullptr;
    CommandHistory* m_history       = nullptr;
    PickerState*    m_picker        = nullptr;

    PropertiesPanel();
    void onRender() override;

private:
    void drawConstraintCubeRef(const char* label, std::string& nameRef,
                               const char* prop,
                               const std::shared_ptr<Instance>& inst);
};
