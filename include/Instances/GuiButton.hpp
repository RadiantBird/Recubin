#pragma once
#include <Instances/ScreenGuiObject.hpp>
#include <Core/RCBNScriptSignal.hpp>

class GuiButton : public ScreenGuiObject {
public:
    std::shared_ptr<RCBNScriptSignal> Activated;
    bool m_wasClickedThisFrame = false;

    explicit GuiButton(std::string className);
    std::string GetClassName() override;
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
};
