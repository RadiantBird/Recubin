#include <Instances/Event.hpp>

Event::Event() : Instance("Event") {
}

void Event::fire() {
    for (auto& wc : m_untilConnections) {
        if (auto c = wc.lock()) c->disconnect();
    }
    m_untilConnections.clear();
}

void Event::addUntilConnection(std::shared_ptr<RCBNScriptConnection> conn) {
    m_untilConnections.emplace_back(conn);
}

std::string Event::getClassName() { return "Event"; }

bool Event::IsA(std::string name) {
    return name == "Event" || Instance::IsA(name);
}
