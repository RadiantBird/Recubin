#include <include/Instances/PointLight.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_pointLightRegistered = []{
    using namespace PropertyRegistry;
    registerClass("PointLight", "LightSource", {});
    return true;
}();

PointLight::PointLight() : Named<PointLight, LightSource>("PointLight") {}

bool PointLight::IsA(std::string className) {
    if (className == "PointLight") return true;
    return LightSource::IsA(className);
}

void PointLight::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "PointLight", name, value)) return;
    LightSource::setProperty(name, value);
}

std::shared_ptr<Instance> PointLight::clone() const {
    auto copy = std::make_shared<PointLight>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "PointLight");  // 基底 LightSource 分も集約
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
