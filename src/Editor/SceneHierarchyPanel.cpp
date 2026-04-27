#include <Editor/SceneHierarchyPanel.hpp>
#include <include/imgui/imgui.h>

// ===================================================
//  SceneHierarchyPanel 実装
// ===================================================

SceneHierarchyPanel::SceneHierarchyPanel()
    : EditorPanel("Scene Hierarchy") {}

void SceneHierarchyPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    if (!workspace) {
        ImGui::TextDisabled("(No workspace)");
        ImGui::End();
        return;
    }

    // ルートノードとして Workspace を表示
    drawNode(workspace);

    ImGui::End();
}

void SceneHierarchyPanel::drawNode(Instance* inst) {
    if (!inst) return;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    // 選択中のノードをハイライト
    if (selectedInstance == inst) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // 子がいなければリーフ表示
    if (inst->getChildren().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        ImGui::TreeNodeEx(inst, flags, "%s  [%s]",
                          inst->Name.c_str(), inst->GetClassName().c_str());
        if (ImGui::IsItemClicked()) {
            selectedInstance = inst;
        }
        return;
    }

    bool open = ImGui::TreeNodeEx(inst, flags, "%s  [%s]",
                                  inst->Name.c_str(), inst->GetClassName().c_str());
    if (ImGui::IsItemClicked()) {
        selectedInstance = inst;
    }

    if (open) {
        for (auto& [name, child] : inst->getChildren()) {
            drawNode(child.get());
        }
        ImGui::TreePop();
    }
}
