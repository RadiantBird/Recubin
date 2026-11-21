#include <Instances/GuiButton.hpp>
#include <include/Core/PropertyRegistry.hpp>

// GuiButton は ScreenGuiObject を継承しボタン用シグナルを持つ（clone 連鎖用に base 登録）
static const bool s_guiButtonRegistered = []{
    using namespace PropertyRegistry;
    registerClass("GuiButton", "ScreenGuiObject", {
        sig<&GuiButton::Activated>("Activated"),
        sig<&GuiButton::HoverEnded>("HoverEnded"),
    });
    return true;
}();

GuiButton::GuiButton(std::string className)
    : Named<GuiButton, ScreenGuiObject>(className)
    , Activated(std::make_shared<RCBNScriptSignal>())
    , HoverEnded(std::make_shared<RCBNScriptSignal>())
{}

bool GuiButton::IsA(std::string name) {
    if (name == "GuiButton") return true;
    return ScreenGuiObject::IsA(name);
}

void GuiButton::setProperty(const std::string& name, const YAML::Node& val) {
    ScreenGuiObject::setProperty(name, val);
}
