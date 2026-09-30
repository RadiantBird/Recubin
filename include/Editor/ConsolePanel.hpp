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

    std::deque<std::string> luauLogs;
    bool luauScrollToBottom = true;
    char luauFilterBuf[256] = {};

    ConsolePanel();
    void onRender() override;
    // ログ本文に使う等幅フォント（補助テキストエディタと共通）。nullptrならUI既定フォント。
    void setLogFont(ImFont* font) { m_logFont = font; }
    void clear();
    void pushLog(const std::string& msg);
    void pushLuauLog(const std::string& msg);

private:
    ImFont* m_logFont = nullptr;
};
