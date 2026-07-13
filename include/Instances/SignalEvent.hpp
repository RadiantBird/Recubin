#pragma once
#include <Instances/Named.hpp>
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>

// Luauスクリプトから任意引数でFire()し、他スクリプトがFired:Connect()で受け取れる汎用イベント。
class SignalEvent : public Named<SignalEvent, Instance> {
public:
    static constexpr const char* ClassName = "SignalEvent";

    std::shared_ptr<RCBNScriptSignal> Fired;

    SignalEvent();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
