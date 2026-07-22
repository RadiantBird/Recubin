#pragma once
#include <cstddef>
#include <string>
class ChatService;
class RuntimeChatOverlay {
public:
    void render(ChatService& service, float x, float y, float width, float height);
    bool isCapturingKeyboard() const { return m_open; }
private:
    bool m_open = false;
    bool m_focusInput = false;
    std::size_t m_seenMessageCount = 0;
    char m_input[513]{};
    std::string m_error;
};
