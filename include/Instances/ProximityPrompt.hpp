#pragma once
#include <Instances/BillboardGui.hpp>
#include <Core/RCBNScriptSignal.hpp>

class ProximityPrompt : public BillboardGui {
public:
    std::string KeyboardKeyCode = "E";
    float HoldDuration = 0.0f;
    float MaxActivationDistance = 10.0f;
    bool Enabled = true;
    std::string ActionText = "Interact";
    std::string ObjectText = "";

    std::shared_ptr<RCBNScriptSignal> Triggered;

    // 内部の状態管理用
    float m_elapsedTime = 0.0f;
    bool m_isHolding = false;
    bool m_hasTriggered = false;
    double m_lastUpdateTime = 0.0;

    ProximityPrompt();
    std::string GetClassName() override;
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
