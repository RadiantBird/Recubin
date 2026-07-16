#include <Instances/GuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_guiObjectRegistered = []{
    using namespace PropertyRegistry;
    registerClass("GuiObject", {
        field   <&GuiObject::Size>   ("Size", 0, 10000, 1),
        enumProp<&GuiObject::NormType>("Norm", {{"Pixel",0},{"Scale",1}}, /*yamlAsString*/true),
        field   <&GuiObject::Active> ("Active"),
        field   <&GuiObject::Visible>("Visible"),
        field   <&GuiObject::BackgroundColor>("BackgroundColor"),
        field   <&GuiObject::ZIndex> ("ZIndex"),
        method_prop<&GuiObject::getTransparency, &GuiObject::setTransparency>("Transparency")
            .noYaml().noClone().noEditor(),
    });
    return true;
}();

GuiObject::GuiObject(std::string className) : Instance(className) {}

bool GuiObject::IsA(std::string name) {
    if (name == "GuiObject") return true;
    return Instance::IsA(name);
}

void GuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "GuiObject", name, val)) return;
    Instance::setProperty(name, val);
}
