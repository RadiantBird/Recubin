#include <Editor/CollisionGroupsPanel.hpp>
#include <Instances/System.hpp>
#include <include/imgui/imgui.h>
#include <algorithm>
#include <cstring>

namespace {
std::string trimWhitespace(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}
}

void CollisionGroupsPanel::onRender() {
    if (!isOpen) return;

    if (dockspaceId != 0) {
        ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    }

    const std::string windowTitle = title.empty() ? "Collision Groups" : title;
    if (!ImGui::Begin(windowTitle.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    if (!m_system) {
        ImGui::TextDisabled("No active System.");
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(200.0f);
    const bool enterPressed = ImGui::InputTextWithHint(
        "##NewCollisionGroup",
        "Group name...",
        m_newGroupName,
        sizeof(m_newGroupName),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add Group") || enterPressed) {
        const std::string trimmed = trimWhitespace(m_newGroupName);
        if (trimmed.empty()) {
            m_statusMessage = "Group name cannot be empty.";
        } else if (m_system->registerCollisionGroup(trimmed)) {
            m_newGroupName[0] = '\0';
            m_statusMessage.clear();
            if (m_onChanged) m_onChanged();
        } else {
            m_statusMessage = "Group already exists.";
        }
    }

    if (!m_statusMessage.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_statusMessage.c_str());
    }

    ImGui::Separator();

    const std::vector<std::string> groups = m_system->getCollisionGroups();
    if (groups.empty()) {
        ImGui::TextDisabled("No collision groups defined.");
        ImGui::End();
        return;
    }

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;

    const int columnCount = static_cast<int>(groups.size()) + 2;
    if (ImGui::BeginTable("##CollisionGroupMatrix", columnCount, tableFlags)) {
        ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        for (const std::string& colGroup : groups) {
            ImGui::TableSetupColumn(colGroup.c_str(), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        }
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < groups.size(); ++row) {
            const std::string& rowGroup = groups[row];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(rowGroup.c_str());

            for (size_t col = 0; col < groups.size(); ++col) {
                ImGui::TableSetColumnIndex(static_cast<int>(col) + 1);
                if (col < row) {
                    ImGui::TextDisabled("-");
                    continue;
                }
                const std::string& colGroup = groups[col];
                bool collidable = m_system->areCollisionGroupsCollidable(rowGroup, colGroup);
                const std::string checkboxId = "##cg_" + rowGroup + "_" + colGroup;
                if (ImGui::Checkbox(checkboxId.c_str(), &collidable)) {
                    m_system->setCollisionGroupsCollidable(rowGroup, colGroup, collidable);
                    if (m_onChanged) m_onChanged();
                }
            }

            ImGui::TableSetColumnIndex(static_cast<int>(groups.size()) + 1);
            if (rowGroup == "Default") {
                ImGui::TextDisabled("-");
            } else {
                const std::string removeId = "Del##del_" + rowGroup;
                if (ImGui::SmallButton(removeId.c_str())) {
                    m_system->unregisterCollisionGroup(rowGroup);
                    m_statusMessage.clear();
                    if (m_onChanged) m_onChanged();
                }
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}
