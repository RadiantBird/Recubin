#include <Instances/WorldGuiObject.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_worldGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("WorldGuiObject", "GuiObject", {});
    return true;
}();

WorldGuiObject::WorldGuiObject(std::string className) : GuiObject(className) {
    Size = {200.f, 100.f};
}

bool WorldGuiObject::IsA(std::string name) {
    if (name == "WorldGuiObject") return true;
    return GuiObject::IsA(name);
}

void WorldGuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "WorldGuiObject", name, val)) return;
    GuiObject::setProperty(name, val);
}
