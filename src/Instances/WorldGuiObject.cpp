#include <Instances/WorldGuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_worldGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("WorldGuiObject", {
        field   <&WorldGuiObject::Size>   ("Size", 0, 10000, 1),
        enumProp<&WorldGuiObject::NormType>("Norm", {{"Pixel",0},{"Scale",1}}, /*yamlAsString*/true),
        field   <&WorldGuiObject::Active> ("Active"),
        field   <&WorldGuiObject::Visible>("Visible"),
        field   <&WorldGuiObject::BackgroundColor>("BackgroundColor"),
        field   <&WorldGuiObject::ZIndex> ("ZIndex"),
        method_prop<&WorldGuiObject::getTransparency, &WorldGuiObject::setTransparency>("Transparency")
            .noYaml().noClone().noEditor(),
    });
    return true;
}();

WorldGuiObject::WorldGuiObject(std::string className) : Instance(className) {}

bool WorldGuiObject::IsA(std::string name) {
    if (name == "WorldGuiObject") return true;
    return Instance::IsA(name);
}

void WorldGuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "WorldGuiObject", name, val)) return;
    Instance::setProperty(name, val);
}
