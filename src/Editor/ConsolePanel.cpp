#include <Editor/ConsolePanel.hpp>
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
        if (ImGui::BeginTabItem("System")) {
            if (ImGui::SmallButton("Clear")) { clear(); }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputText("Filter##sys", filterBuf, sizeof(filterBuf));
            ImGui::Separator();

            ImGui::BeginChild("SysScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                               ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& line : logs) {
                if (filterBuf[0] != '\0' && line.find(filterBuf) == std::string::npos) continue;
                ImVec4 col = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                if (line.starts_with("[WARN]"))  col = ImVec4(1.0f, 0.85f, 0.0f, 1.0f);
                if (line.starts_with("[ERROR]")) col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
                if (line.starts_with("[LOG]"))   col = ImVec4(0.7f, 0.9f, 1.0f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (scrollToBottom) { ImGui::SetScrollHereY(1.0f); scrollToBottom = false; }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ---- Luau タブ ----
        if (ImGui::BeginTabItem("Luau")) {
            if (ImGui::SmallButton("Clear##luau")) { luauLogs.clear(); }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputText("Filter##luau", luauFilterBuf, sizeof(luauFilterBuf));
            ImGui::Separator();

            ImGui::BeginChild("LuauScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                               ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& line : luauLogs) {
                if (luauFilterBuf[0] != '\0' && line.find(luauFilterBuf) == std::string::npos) continue;
                ImVec4 col = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                if (line.starts_with("[ERROR]")) col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
                if (line.starts_with("[WARN]"))  col = ImVec4(1.0f, 0.85f, 0.0f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (luauScrollToBottom) { ImGui::SetScrollHereY(1.0f); luauScrollToBottom = false; }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
