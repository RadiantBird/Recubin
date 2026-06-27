#include <include/Instances/Lighting.hpp>
#include <include/Core/PropertyRegistry.hpp>

// プロパティ・スキーマ（単一の正）。Luau/YAML/clone/エディターを一括駆動。
static const bool s_lightingRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Lighting", {
        field<&Lighting::lightDir>  ("Direction",  -1.0f, 1.0f, 0.01f),
        field<&Lighting::brightness>("Brightness",  0.0f, 5.0f, 0.01f),
        field<&Lighting::lightColor>("Color"),
    });
    return true;
}();

Lighting::Lighting() : Instance("Lighting") {}

std::string Lighting::getClassName() { return "Lighting"; }

bool Lighting::IsA(std::string className) {
    if (className == "Lighting") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> Lighting::clone() const {
    auto copy = std::make_shared<Lighting>();
    PropertyRegistry::cloneFields(this, copy.get(), "Lighting");
    return copy;
}

void Lighting::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Lighting", name, value)) return;
    Instance::setProperty(name, value);
}
