#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Script.hpp>
#include <include/imgui/imgui.h>
#include <fstream>

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

    drawNode(workspace);

    renderNewScriptDialog();

    ImGui::End();
}

void SceneHierarchyPanel::drawNode(Instance* inst) {
    if (!inst) return;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    if (selectedInstance == inst) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool isLeaf = inst->getChildren().empty();
    if (isLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    bool open = ImGui::TreeNodeEx(inst, flags, "%s  [%s]",
                                  inst->Name.c_str(), inst->GetClassName().c_str());
    if (ImGui::IsItemClicked()) {
        selectedInstance = inst;
    }

    // ---- 右クリックコンテキストメニュー ----
    std::string popupId = "ctx##" + std::to_string(reinterpret_cast<uintptr_t>(inst));
    if (ImGui::BeginPopupContextItem(popupId.c_str())) {
        // --- Insert Object ---
        if (ImGui::BeginMenu("Insert Object")) {
            if (ImGui::MenuItem("Cube") && m_history) {
                auto parentSp = inst->shared_from_this();
                auto cube = std::make_shared<Cube>(Vector3(0, 5, 0), Vector3(1, 1, 1),
                                                   Cube::defaultTextureID);
                std::string name = "Cube";
                int n = 1;
                while (parentSp->children.count(name) > 0)
                    name = "Cube" + std::to_string(n++);
                cube->Name = name;
                m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, cube));
            }
            if (ImGui::MenuItem("Script")) {
                m_pendingScriptParent = inst->shared_from_this();
                m_openScriptDialog    = true;
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // --- Delete ---
        bool doDelete = ImGui::MenuItem("Delete", "BackSpace");
        if (doDelete && m_history) {
            auto parent = inst->Parent.lock();
            if (parent) {
                auto childPtr = parent->children.at(inst->Name);
                m_history->execute(std::make_unique<RemoveInstanceCommand>(
                    parent, inst->Name, childPtr));
                if (selectedInstance == inst) selectedInstance = nullptr;
            }
        }

        ImGui::Separator();

        // --- Copy ---
        if (ImGui::MenuItem("Copy", "Ctrl+C") && m_clipboard) {
            *m_clipboard = inst->clone();
        }

        // --- Paste (sibling) ---
        bool canPaste = m_clipboard && *m_clipboard;
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {
            if (canPaste && m_history) {
                auto parent = inst->Parent.lock();
                if (parent) {
                    auto cloned = (*m_clipboard)->clone();
                    std::string base = cloned->Name;
                    int n = 1;
                    while (parent->children.count(cloned->Name) > 0)
                        cloned->Name = base + std::to_string(n++);
                    m_history->execute(std::make_unique<AddInstanceCommand>(parent, cloned));
                }
            }
        }

        // --- Paste as Child ---
        if (ImGui::MenuItem("Paste as Child", "Ctrl+Shift+V", false, canPaste)) {
            if (canPaste && m_history) {
                auto parentSp = inst->shared_from_this();
                auto cloned = (*m_clipboard)->clone();
                std::string base = cloned->Name;
                int n = 1;
                while (parentSp->children.count(cloned->Name) > 0)
                    cloned->Name = base + std::to_string(n++);
                m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, cloned));
            }
        }

        ImGui::EndPopup();
    }

    if (!isLeaf && open) {
        for (auto& [name, child] : inst->getChildren()) {
            drawNode(child.get());
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::renderNewScriptDialog() {
    if (m_openScriptDialog) {
        ImGui::OpenPopup("New Script");
        m_openScriptDialog = false;
    }

    if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char s_name[128] = "NewScript";
        ImGui::Text("Script name:");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("##sname", s_name, sizeof(s_name));

        if (ImGui::Button("OK", ImVec2(100, 0))) {
            std::string path = std::string("assets/scripts/") + s_name + ".lua";
            std::ofstream f(path);
            if (f) f << "-- " << s_name << "\n";

            if (m_pendingScriptParent && m_history) {
                auto script = std::make_shared<Script>(path);
                script->Name = std::string(s_name);
                m_history->execute(std::make_unique<AddInstanceCommand>(
                    m_pendingScriptParent, script));
            }
            m_pendingScriptParent.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pendingScriptParent.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
