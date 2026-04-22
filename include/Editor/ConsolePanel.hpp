#pragma once
#include <Editor/EditorPanel.hpp>
#include <vector>
#include <deque>
#include <string>
#include <include/imgui/imgui.h>

// ===================================================
//  ConsolePanel  — RCBN_LOG の出力をキャプチャして表示
// ===================================================
class ConsolePanel : public EditorPanel {
public:
    static constexpr int MAX_LOG = 512;

    std::deque<std::string> logs;
    bool scrollToBottom = true;
    char filterBuf[256] = {};

    ConsolePanel();
    void onRender() override;
    void clear();
    void pushLog(const std::string& msg);
};
