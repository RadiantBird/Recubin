#include <Instances/ScreenGuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_screenGuiRegistered = []{
    using namespace PropertyRegistry;
    auto systemFont = enumProp<&ScreenGuiObject::Font>(
        "Font", {{"Default", 0}, {"DotGothic16", 1}}, true);
    systemFont.noEditor();
    registerClass("ScreenGuiObject", "GuiObject", {
        field<&ScreenGuiObject::Position>("Position", 0, 0, 1),
        field<&ScreenGuiObject::UseFontFile>("UseFontFile"),
        std::move(systemFont),
        instanceRefField<&ScreenGuiObject::FontFile>("FontFile", "FontFile")
            .omitEmpty().noEditor(),
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
    // 旧形式の Font: <FontFile.Name> を、新しい FontFile 参照へ移す。
    if (name == "Font" && val.IsScalar()) {
        const std::string legacy = val.as<std::string>("Default");
        if (legacy != "Default" && legacy != "DotGothic16") {
            FontFile = legacy;
            UseFontFile = true;
            Font = SystemFont::Default;
            return;
        }
    }
    if (name == "FontFile" && val.IsScalar() && !val.as<std::string>("").empty())
        UseFontFile = true;
    if (PropertyRegistry::loadProperty(this, "ScreenGuiObject", name, val)) return;
    GuiObject::setProperty(name, val);
}
