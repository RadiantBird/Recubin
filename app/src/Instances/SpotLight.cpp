#include <include/Instances/SpotLight.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_spotLightRegistered = []{
    using namespace PropertyRegistry;
    registerClass("SpotLight", "LightSource", {
        field<&SpotLight::Angle>("Angle", 1.0f, 89.0f, 0.5f).clampLua(),
    });
    return true;
}();

SpotLight::SpotLight() : Named<SpotLight, LightSource>("SpotLight") {}

bool SpotLight::IsA(std::string className) {
    if (className == "SpotLight") return true;
    return LightSource::IsA(className);
}

void SpotLight::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "SpotLight", name, value)) return;
    LightSource::setProperty(name, value);
}

std::shared_ptr<Instance> SpotLight::clone() const {
    auto copy = std::make_shared<SpotLight>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "SpotLight");  // 基底 LightSource 分も集約
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
