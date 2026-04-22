#include <Editor/ConsolePanel.hpp>
#include <Util/Logger.hpp>
#include <include/imgui/imgui.h>

// ===================================================
//  ConsolePanel 実装
// ===================================================

ConsolePanel::ConsolePanel() : EditorPanel("Console") {
    // Logger フックに自分の pushLog を登録
    g_logHook = [this](const std::string& msg) {
        this->pushLog(msg);
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

void ConsolePanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    // ツールバー
    if (ImGui::SmallButton("Clear")) { clear(); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter", filterBuf, sizeof(filterBuf));

    ImGui::Separator();

    // ログリスト
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& line : logs) {
        // フィルター
        if (filterBuf[0] != '\0' &&
            line.find(filterBuf) == std::string::npos) {
            continue;
        }

        // カラーリング
        ImVec4 col = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
        if (line.starts_with("[WARN]"))  col = ImVec4(1.0f, 0.85f, 0.0f, 1.0f);
        if (line.starts_with("[ERROR]")) col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        if (line.starts_with("[LOG]"))   col = ImVec4(0.7f, 0.9f, 1.0f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }

    if (scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom = false;
    }

    ImGui::EndChild();
    ImGui::End();
}
