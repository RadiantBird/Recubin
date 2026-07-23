#include <Editor/ConsolePanel.hpp>
#include <Editor/Localization.hpp>
#include <Util/Logger.hpp>
#include <include/imgui/imgui.h>

// ===================================================
//  ConsolePanel 実装
// ===================================================

ConsolePanel::ConsolePanel() : EditorPanel("Console") {
    g_logHook = [this](const std::string& msg) {
        this->pushLog(msg);
    };
    g_luauLogHook = [this](const std::string& msg) {
        this->pushLuauLog(msg);
    };
    pushLog("[LOG] Console initialized.");
}

void ConsolePanel::pushLog(const std::string& msg) {
    if ((int)logs.size() >= MAX_LOG) {
        logs.pop_front();
    }
    logs.push_back(msg);
    scrollToBottom = true;
}

void ConsolePanel::clear() {
    logs.clear();
}

void ConsolePanel::pushLuauLog(const std::string& msg) {
    if ((int)luauLogs.size() >= MAX_LOG) {
        luauLogs.pop_front();
    }
    luauLogs.push_back(msg);
    luauScrollToBottom = true;
}

void ConsolePanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("ConsoleTabs")) {

        // ---- System タブ ----
        if (ImGui::BeginTabItem(Loc::t(Loc::LocKey::TabSystem))) {
            if (ImGui::SmallButton(Loc::t(Loc::LocKey::ClearButton))) { clear(); }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            std::string filterLabel = std::string(Loc::t(Loc::LocKey::FilterLabel)) + "##sys";
            ImGui::InputText(filterLabel.c_str(), filterBuf, sizeof(filterBuf));
            ImGui::SameLine();
            std::string sysCopyLabel = std::string(Loc::t(Loc::LocKey::MenuCopy)) + "##syscopy";
            if (ImGui::SmallButton(sysCopyLabel.c_str())) {
                std::string joined;
                for (const auto& line : logs) {
                    if (filterBuf[0] != '\0' && line.find(filterBuf) == std::string::npos) continue;
                    joined += line;
                    joined += '\n';
                }
                ImGui::SetClipboardText(joined.c_str());
            }
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f, 0.060f, 0.105f, 1.0f));
            ImGui::BeginChild("SysScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                               ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& line : logs) {
                if (filterBuf[0] != '\0' && line.find(filterBuf) == std::string::npos) continue;
                ImVec4 col = ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
                if (line.starts_with("[WARN]"))   col = ImVec4(0.90f, 0.70f, 0.18f, 1.0f);
                if (line.starts_with("[ERROR]"))  col = ImVec4(0.90f, 0.30f, 0.32f, 1.0f);
                if (line.starts_with("[TRACE]"))  col = ImVec4(0.48f, 0.60f, 0.72f, 1.0f);
                if (line.starts_with("[LOG]"))    col = ImVec4(0.68f, 0.76f, 0.84f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (scrollToBottom) { ImGui::SetScrollHereY(1.0f); scrollToBottom = false; }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::EndTabItem();
        }

        // ---- Luau タブ ----
        if (ImGui::BeginTabItem(Loc::t(Loc::LocKey::TabLuau))) {
            std::string clearLabel = std::string(Loc::t(Loc::LocKey::ClearButton)) + "##luau";
            if (ImGui::SmallButton(clearLabel.c_str())) { luauLogs.clear(); }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            std::string filterLabel = std::string(Loc::t(Loc::LocKey::FilterLabel)) + "##luau";
            ImGui::InputText(filterLabel.c_str(), luauFilterBuf, sizeof(luauFilterBuf));
            ImGui::SameLine();
            std::string luauCopyLabel = std::string(Loc::t(Loc::LocKey::MenuCopy)) + "##luaucopy";
            if (ImGui::SmallButton(luauCopyLabel.c_str())) {
                std::string joined;
                for (const auto& line : luauLogs) {
                    if (luauFilterBuf[0] != '\0' && line.find(luauFilterBuf) == std::string::npos) continue;
                    joined += line;
                    joined += '\n';
                }
                ImGui::SetClipboardText(joined.c_str());
            }
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f, 0.060f, 0.105f, 1.0f));
            ImGui::BeginChild("LuauScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                               ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& line : luauLogs) {
                if (luauFilterBuf[0] != '\0' && line.find(luauFilterBuf) == std::string::npos) continue;
                ImVec4 col = ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
                if (line.starts_with("[ERROR]")) col = ImVec4(0.90f, 0.30f, 0.32f, 1.0f);
                if (line.starts_with("[WARN]"))  col = ImVec4(0.90f, 0.70f, 0.18f, 1.0f);
                if (line.starts_with("[TRACE]")) col = ImVec4(0.48f, 0.60f, 0.72f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (luauScrollToBottom) { ImGui::SetScrollHereY(1.0f); luauScrollToBottom = false; }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
