#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/BaseCube.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CommandHistory;

// キューブ/Attachment指定ピッカーの共有状態（EditorManager / PropertiesPanel / ViewportPanel で共有）
struct PickerState {
    bool        active          = false;
    bool        pickAttachment  = false;                           // true のとき BaseCube ではなく Attachment を対象にする
    bool        pickAnyInstance = false;                           // true のとき型制限なしで任意のInstanceを対象にする（ObjectValue用）
    std::string prop;                                              // "Cube0"/"Cube1"/"Attachment0"/"Attachment1"
    Instance*   constraint = nullptr;
    std::function<void(std::shared_ptr<Instance>)> onPick;         // pickAttachment に応じた型のインスタンスが渡される
};

// Terrainブラシの共有状態（EditorManager / PropertiesPanel / ViewportPanel で共有）
struct TerrainBrushState {
    bool  active    = false;
    float radius    = 8.0f;   // studs
    int   mode      = +1;     // Sculpt時のみ: +1=Raise, -1=Lower, 0=Smooth
    bool  paintMode = false;  // false=Sculpt, true=Paint
    float paintColor[3] = { 0.78f, 0.78f, 0.78f }; // ImGui::ColorEdit3用（0.0-1.0）
};

// Decal配置モードの共有状態（EditorManager / PropertiesPanel / ViewportPanel で共有）
struct DecalPlaceState {
    bool active = false;
};

// ===================================================
//  PropertiesPanel  — 選択中インスタンスのプロパティ編集
// ===================================================
class PropertiesPanel : public EditorPanel {
public:
    Instance**     selectedInstance = nullptr;
    std::vector<Instance*>* selectedInstances = nullptr;
    CommandHistory* m_history       = nullptr;
    PickerState*    m_picker        = nullptr;
    TerrainBrushState* m_terrainBrush = nullptr;
    DecalPlaceState*   m_decalPlace   = nullptr;

    PropertiesPanel();
    void onRender() override;

private:
    void drawConstraintCubeRef(const char* label, std::string& nameRef,
                               const char* prop,
                               const std::shared_ptr<Instance>& inst);
    // Attachment名（対応Cube配下の子孫パス）の参照フィールド。手入力＋Pickボタン。
    // cubeName は対応する Cube0/Cube1 の名前（Pick時の配下チェックに使う）
    void drawConstraintAttachmentRef(const char* label, std::string& nameRef,
                                     const char* prop, const std::string& cubeName,
                                     const std::shared_ptr<Instance>& inst);

    // ObjectValue.Value 用: テキストのパス入力欄のみ(ビューポートPickボタンは無し)
    void drawObjectValueRef(const char* label, const std::shared_ptr<Instance>& inst);

    double m_terrainRegenOpenedAt = 0.0;
};
