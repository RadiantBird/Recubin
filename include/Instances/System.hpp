#pragma once
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <memory>

class System : public Instance {
    public:
        std::shared_ptr<RCBNScriptSignal> Heartbeat;

        System(string name = "System");
        string GetClassName() override;
        bool IsA(std::string className) override;
};