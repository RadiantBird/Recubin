#include <Editor/PropertiesPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/BaseCube.hpp>
#include <Util/Color4.hpp>
#include <include/imgui/imgui.h>
#include <unordered_map>
#include <string>

// ===================================================
//  PropertiesPanel 実装
// ===================================================

PropertiesPanel::PropertiesPanel()
    : EditorPanel("Properties") {}

// ===================================================
//  Vector3 一括入力 + 展開式フィールド
//  collapsed: InputText "x, y, z"  [▼]
//  expanded : DragFloat × 3         [▲]
// ===================================================
static void drawVec3Field(const char* id,
                          Vector3& val,
                          float speed, float minVal, float maxVal,
                          std::shared_ptr<BaseCube> bc,
                          const std::string& prop,
                          CommandHistory* history)
{
    static std::unordered_map<std::string, bool>    s_exp;
    static std::unordered_map<std::string, Vector3> s_before;
    bool& expanded = s_exp[id];

    // 折りたたみ: InputText "x, y, z"
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", val.x, val.y, val.z);

        float w = ImGui::GetContentRegionAvail().x - 34.0f;
        if (w < 60.0f) w = 60.0f;
        ImGui::SetNextItemWidth(w);

        std::string txtId = std::string("##txt_") + id;
        if (ImGui::InputText(txtId.c_str(), buf, sizeof(buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            float x = val.x, y = val.y, z = val.z;
            if (sscanf(buf, "%f , %f , %f", &x, &y, &z) == 3 ||
                sscanf(buf, "%f,%f,%f",     &x, &y, &z) == 3) {
                Vector3 newVal(x, y, z);
                if (history && bc) {
                    history->execute(std::make_unique<SetVec3Command>(bc, prop, val, newVal));
                } else if (bc && prop == "Position") {
                    bc->teleportTo(newVal);
                } else if (bc && prop == "Size") {
                    bc->setSize(newVal);
                } else {
                    val = newVal;
                }
            }
        }
        ImGui::SameLine();
        std::string btnId = std::string(expanded ? "-##col_" : "+##exp_") + id;
        if (ImGui::SmallButton(btnId.c_str())) expanded = !expanded;
    }

    // 展開: DragFloat3
    if (expanded) {
        ImGui::Indent(12.0f);
        ImGui::PushID(id);

        std::string key = std::string(id) + "_before";
        float arr[3] = { val.x, val.y, val.z };
        bool changed = ImGui::DragFloat3("##drag", arr, speed, minVal, maxVal);

        if (ImGui::IsItemActivated()) s_before[key] = val;

        if (changed) {
            Vector3 newVal(arr[0], arr[1], arr[2]);
            if (bc && prop == "Position") bc->teleportTo(newVal);
            else if (bc && prop == "Size") bc->setSize(newVal);
            else val = newVal;
        }

        if (ImGui::IsItemDeactivatedAfterEdit() && history && bc) {
            Vector3 after(arr[0], arr[1], arr[2]);
            history->record(std::make_unique<SetVec3Command>(bc, prop, s_before[key], after));
        }

        ImGui::PopID();
        ImGui::Unindent(12.0f);
    }
}

void PropertiesPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    Instance* inst = selectedInstance ? *selectedInstance : nullptr;

    if (!inst) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
    }

    // ---- 基本情報 ----
    ImGui::SeparatorText("Instance");
    ImGui::LabelText("ClassName", "%s", inst->GetClassName().c_str());
    ImGui::LabelText("Path",      "%s", inst->getFullPath().c_str());

    char nameBuf[256] = {};
    strncpy_s(nameBuf, inst->Name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        inst->Name = std::string(nameBuf);
    }

    // ---- Spatial (Position / Size) ----
    if (inst->IsA("Spatial")) {
        Spatial* s = static_cast<Spatial*>(inst);
        std::shared_ptr<BaseCube> bcSp;
        if (inst->IsA("BaseCube")) {
            bcSp = std::static_pointer_cast<BaseCube>(inst->shared_from_this());
        }

        ImGui::SeparatorText("Transform");

        ImGui::Text("Position");
        ImGui::SameLine(80.0f);
        drawVec3Field("Position", s->Position, 0.05f, -1e9f, 1e9f, bcSp, "Position", m_history);

        ImGui::Text("Size");
        ImGui::SameLine(80.0f);
        drawVec3Field("Size", s->Size, 0.05f, 0.01f, 1000.0f, bcSp, "Size", m_history);
    }

    // ---- BaseCube (Color / Anchored / CanCollide) ----
    if (inst->IsA("BaseCube")) {
        BaseCube* bc = static_cast<BaseCube*>(inst);
        auto bcSp = std::static_pointer_cast<BaseCube>(inst->shared_from_this());

        ImGui::SeparatorText("Appearance");

        // Color with undo
        static Color4 s_colorBefore;
        float col[4] = { bc->Color.r, bc->Color.g, bc->Color.b, bc->Color.a };
        if (ImGui::IsItemActivated()) s_colorBefore = bc->Color;
        bool colorChanged = ImGui::ColorEdit4("Color", col);
        if (ImGui::IsItemActivated()) s_colorBefore = bc->Color;
        if (colorChanged) bc->Color = Color4(col[0], col[1], col[2], col[3]);
        if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
            Color4 after(col[0], col[1], col[2], col[3]);
            m_history->record(std::make_unique<SetColorCommand>(bcSp, s_colorBefore, after));
        }

        ImGui::SeparatorText("Physics");

        // Anchored with undo
        bool prevAnchored = bc->Anchored;
        if (ImGui::Checkbox("Anchored", &bc->Anchored) && m_history) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "Anchored", prevAnchored, bc->Anchored));
        }

        // CanCollide with undo
        bool prevCanCollide = bc->CanCollide;
        if (ImGui::Checkbox("CanCollide", &bc->CanCollide) && m_history) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "CanCollide", prevCanCollide, bc->CanCollide));
        }
    }

    ImGui::End();
}
