#include <Instances/ScreenGuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_screenGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ScreenGuiObject", {
        field   <&ScreenGuiObject::Position>("Position", 0, 0, 1),
        field   <&ScreenGuiObject::Size>    ("Size",     0, 10000, 1),
        enumProp<&ScreenGuiObject::NormType>("Norm", {{"Pixel",0},{"Scale",1}}, /*yamlAsString*/true),
        field   <&ScreenGuiObject::Visible> ("Visible"),
        field   <&ScreenGuiObject::Active>  ("Active"),
        field   <&ScreenGuiObject::ZIndex>  ("ZIndex"),
        field   <&ScreenGuiObject::BackgroundColor>("BackgroundColor"),
        method_prop<&ScreenGuiObject::getTransparency, &ScreenGuiObject::setTransparency>("Transparency")
            .noYaml().noClone().noEditor(),
        sig     <&ScreenGuiObject::Hovered>("Hovered"),
    });
    return true;
}();

ScreenGuiObject::ScreenGuiObject(std::string className)
    : Instance(className)
    , Hovered(std::make_shared<RCBNScriptSignal>())
{}

bool ScreenGuiObject::IsA(std::string name) {
    if (name == "ScreenGuiObject") return true;
    return Instance::IsA(name);
}

void ScreenGuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "ScreenGuiObject", name, val)) return;
    Instance::setProperty(name, val);
}
