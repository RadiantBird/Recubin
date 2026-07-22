#pragma once
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <Network/NetworkTypes.hpp>
#include <deque>
#include <functional>

class ChatService : public Instance {
public:
    struct Message { PeerId senderId; std::string text; };
    static constexpr size_t MaxMessageBytes = 512;
    static constexpr size_t MaxHistory = 100;
    std::shared_ptr<RCBNScriptSignal> MessageReceived;
    std::function<void(const std::string&)> onSendRequested;
    ChatService();
    std::string getClassName() override { return "ChatService"; }
    bool IsA(std::string className) override;
    bool sendMessage(const std::string& text);
    void receiveMessage(PeerId senderId, const std::string& text);
    const std::deque<Message>& messages() const { return m_messages; }
    static bool isValidMessage(const std::string& text);
private:
    std::deque<Message> m_messages;
};
