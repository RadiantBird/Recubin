#include <Editor/ConsolePanel.hpp>
#include <Editor/Localization.hpp>
#include <Util/Logger.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_internal.h>
#include <cfloat>
#include <cmath>

namespace {
ImVec4 consoleLogColor(const std::string& line, bool systemLog) {
    if (line.starts_with("[WARN]")) return ImVec4(0.90f, 0.70f, 0.18f, 1.0f);
    if (line.starts_with("[ERROR]")) return ImVec4(0.90f, 0.30f, 0.32f, 1.0f);
    if (line.starts_with("[TRACE]")) return ImVec4(0.48f, 0.60f, 0.72f, 1.0f);
    if (systemLog && line.starts_with("[LOG]")) return ImVec4(0.68f, 0.76f, 0.84f, 1.0f);
    return ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
}

std::vector<std::pair<std::string, ImVec4>> splitColoredLines(
    const std::deque<std::string>& logs, const char* filter, bool systemLog) {
    std::vector<std::pair<std::string, ImVec4>> result;
    for (const auto& entry : logs) {
        if (filter[0] != '\0' && entry.find(filter) == std::string::npos) continue;
        const ImVec4 color = consoleLogColor(entry, systemLog);
        size_t begin = 0;
        for (;;) {
            const size_t end = entry.find('\n', begin);
            result.emplace_back(entry.substr(begin, end == std::string::npos ? std::string::npos : end - begin), color);
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    return result;
}

void drawColoredConsoleText(ImGuiWindow* parent, ImGuiID inputId,
                            const std::vector<std::pair<std::string, ImVec4>>& lines) {
    ImGuiWindow* child = nullptr;
    for (int i = parent->DC.ChildWindows.Size - 1; i >= 0; --i) {
        ImGuiWindow* candidate = parent->DC.ChildWindows[i];
        if (candidate != nullptr && candidate->ChildId == inputId) { child = candidate; break; }
    }
    if (child == nullptr) return;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 origin(child->DC.CursorStartPos.x + style.FramePadding.x,
                 child->DC.CursorStartPos.y + style.FramePadding.y);
    if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(inputId);
        inputState != nullptr && inputState->ID == inputId) {
        origin.x -= inputState->Scroll.x;
    }
    const ImRect clip = child->InnerClipRect;
    const ImVec4 clipRect = clip.ToVec4();
    const float lineHeight = ImGui::GetFontSize();
    const int firstLine = ImMax(0, (int)std::floor((clip.Min.y - origin.y) / lineHeight));
    const int lastLine = ImMin((int)lines.size(), (int)std::ceil((clip.Max.y - origin.y) / lineHeight));
    for (int lineNo = firstLine; lineNo < lastLine; ++lineNo) {
        const std::string& line = lines[lineNo].first;
        if (!line.empty()) {
            child->DrawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(origin.x, origin.y + lineNo * lineHeight),
                ImGui::GetColorU32(lines[lineNo].second),
                line.c_str(), line.c_str() + line.size(), 0.0f, &clipRect);
        }
    }
}
}

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
            std::string joined;
            for (const auto& line : logs) {
                if (filterBuf[0] != '\0' && line.find(filterBuf) == std::string::npos) continue;
                joined += line;
                joined += '\n';
            }
            ImGui::SameLine();
            std::string sysCopyLabel = std::string(Loc::t(Loc::LocKey::MenuCopy)) + "##syscopy";
            if (ImGui::SmallButton(sysCopyLabel.c_str())) {
                ImGui::SetClipboardText(joined.c_str());
            }
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.045f, 0.060f, 0.105f, 1.0f));
            if (scrollToBottom) { ImGui::SetNextWindowScroll(ImVec2(-1.0f, FLT_MAX)); }
            ImGuiWindow* sysParent = GImGui->CurrentWindow;
            const ImGuiID sysInputId = sysParent->GetID("##SysLog");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_InputTextCursor, ImVec4(0, 0, 0, 0));
            ImGui::InputTextMultiline("##SysLog", joined.data(), joined.size() + 1,
                                      ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor(2);
            drawColoredConsoleText(sysParent, sysInputId,
                                   splitColoredLines(logs, filterBuf, true));
            scrollToBottom = false;
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
            std::string joined;
            for (const auto& line : luauLogs) {
                if (luauFilterBuf[0] != '\0' && line.find(luauFilterBuf) == std::string::npos) continue;
                joined += line;
                joined += '\n';
            }
            ImGui::SameLine();
            std::string luauCopyLabel = std::string(Loc::t(Loc::LocKey::MenuCopy)) + "##luaucopy";
            if (ImGui::SmallButton(luauCopyLabel.c_str())) {
                ImGui::SetClipboardText(joined.c_str());
            }
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.045f, 0.060f, 0.105f, 1.0f));
            if (luauScrollToBottom) { ImGui::SetNextWindowScroll(ImVec2(-1.0f, FLT_MAX)); }
            ImGuiWindow* luauParent = GImGui->CurrentWindow;
            const ImGuiID luauInputId = luauParent->GetID("##LuauLog");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_InputTextCursor, ImVec4(0, 0, 0, 0));
            ImGui::InputTextMultiline("##LuauLog", joined.data(), joined.size() + 1,
                                      ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor(2);
            drawColoredConsoleText(luauParent, luauInputId,
                                   splitColoredLines(luauLogs, luauFilterBuf, false));
            luauScrollToBottom = false;
            ImGui::PopStyleColor();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
