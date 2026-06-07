#include <Instances/GuiButton.hpp>

GuiButton::GuiButton(std::string className)
    : ScreenGuiObject(className)
    , Activated(std::make_shared<RCBNScriptSignal>())
{}

std::string GuiButton::getClassName() { return "GuiButton"; }

bool GuiButton::IsA(std::string name) {
    if (name == "GuiButton") return true;
    return ScreenGuiObject::IsA(name);
}

void GuiButton::setProperty(const std::string& name, const YAML::Node& val) {
    ScreenGuiObject::setProperty(name, val);
}
