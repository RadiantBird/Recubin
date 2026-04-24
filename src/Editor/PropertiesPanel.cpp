#include <Editor/PropertiesPanel.hpp>
#include <Instances/BaseCube.hpp>
#include <Util/Color4.hpp>
#include <include/imgui/imgui.h>

// ===================================================
//  PropertiesPanel 実装
// ===================================================

PropertiesPanel::PropertiesPanel()
    : EditorPanel("Properties") {}

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

    // 名前の編集
    char nameBuf[256] = {};
    strncpy_s(nameBuf, inst->Name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        inst->Name = std::string(nameBuf);
    }

    // ---- Spatial (Position / Size) ----
    if (inst->IsA("Spatial")) {
        Spatial* s = static_cast<Spatial*>(inst);

        ImGui::SeparatorText("Transform");

        float pos[3] = { s->Position.x, s->Position.y, s->Position.z };
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            if (inst->IsA("BaseCube")) {
                static_cast<BaseCube*>(inst)->teleportTo(Vector3(pos[0], pos[1], pos[2]));
            } else {
                s->Position = Vector3(pos[0], pos[1], pos[2]);
            }
        }

        float size[3] = { s->Size.x, s->Size.y, s->Size.z };
        if (ImGui::DragFloat3("Size", size, 0.05f, 0.01f, 1000.0f)) {
            if (inst->IsA("BaseCube")) {
                static_cast<BaseCube*>(inst)->setSize(Vector3(size[0], size[1], size[2]));
            } else {
                s->Size = Vector3(size[0], size[1], size[2]);
            }
        }
    }

    // ---- BaseCube (Color / Anchored / CanCollide) ----
    if (inst->IsA("BaseCube")) {
        BaseCube* bc = static_cast<BaseCube*>(inst);

        ImGui::SeparatorText("Appearance");
        float col[4] = { bc->Color.r, bc->Color.g, bc->Color.b, bc->Color.a };
        if (ImGui::ColorEdit4("Color", col)) {
            bc->Color = Color4(col[0], col[1], col[2], col[3]);
        }

        ImGui::SeparatorText("Physics");
        ImGui::Checkbox("Anchored",   &bc->Anchored);
        ImGui::Checkbox("CanCollide", &bc->CanCollide);
    }

    ImGui::End();
}
