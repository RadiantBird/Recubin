#include <Instances/ScreenGuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_screenGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ScreenGuiObject", "GuiObject", {
        field<&ScreenGuiObject::Position>("Position", 0, 0, 1),
        sig  <&ScreenGuiObject::Hovered>("Hovered"),
    });
    return true;
}();

ScreenGuiObject::ScreenGuiObject(std::string className)
    : GuiObject(className)
    , Hovered(std::make_shared<RCBNScriptSignal>())
{}

bool ScreenGuiObject::IsA(std::string name) {
    if (name == "ScreenGuiObject") return true;
    return GuiObject::IsA(name);
}

void ScreenGuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "ScreenGuiObject", name, val)) return;
    GuiObject::setProperty(name, val);
}
