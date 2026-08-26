#pragma once
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Named.hpp>
#include <Core/RCBNScriptSignal.hpp>

class GuiButton : public Named<GuiButton, ScreenGuiObject> {
public:
    static constexpr const char* ClassName = "GuiButton";

    std::shared_ptr<RCBNScriptSignal> Activated;
    std::shared_ptr<RCBNScriptSignal> HoverEnded;

    explicit GuiButton(std::string className);
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
};
