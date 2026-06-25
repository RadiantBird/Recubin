#include <Instances/BillboardGui.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_billboardGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("BillboardGui", "WorldGuiObject", {
        enumProp<&BillboardGui::Mode>("Mode", {{"Parallel",0},{"Focus",1}}, /*yamlAsString*/true),
    });
    return true;
}();

BillboardGui::BillboardGui() : Named<BillboardGui, WorldGuiObject>("BillboardGui") {}

bool BillboardGui::IsA(std::string name) {
    if (name == "BillboardGui") return true;
    return WorldGuiObject::IsA(name);
}

void BillboardGui::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "BillboardGui", name, val)) return;
    WorldGuiObject::setProperty(name, val);
}

std::shared_ptr<Instance> BillboardGui::clone() const {
    auto copy = std::make_shared<BillboardGui>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "BillboardGui");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
