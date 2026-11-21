#pragma once
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <memory>
#include <vector>

class Event : public Instance {
public:
    std::vector<std::weak_ptr<RCBNScriptConnection>> m_untilConnections;

    Event();

    void fire();
    void addUntilConnection(std::shared_ptr<RCBNScriptConnection> conn);

    std::string getClassName() override;
    bool IsA(std::string name) override;
};
