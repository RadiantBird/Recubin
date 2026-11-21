#include <Instances/ChatService.hpp>

ChatService::ChatService() : Instance("ChatService"), MessageReceived(std::make_shared<RCBNScriptSignal>()) {}
bool ChatService::IsA(std::string className) { return className == "ChatService" || Instance::IsA(className); }
bool ChatService::isValidMessage(const std::string& text) {
    if (text.empty() || text.size() > MaxMessageBytes) return false;
    for (unsigned char c : text) if (c < 0x20 || c == 0x7f) return false;
    return true;
}
bool ChatService::sendMessage(const std::string& text) {
    if (!isValidMessage(text) || !onSendRequested) return false;
    onSendRequested(text); return true;
}
void ChatService::receiveMessage(PeerId senderId, const std::string& text) {
    if (senderId == 0 || !isValidMessage(text)) return;
    m_messages.push_back({senderId, text});
    while (m_messages.size() > MaxHistory) m_messages.pop_front();
}
