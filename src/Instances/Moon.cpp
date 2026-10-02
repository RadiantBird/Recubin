#include <Instances/Moon.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_moonRegistered = []{
    CelestialBody::registerSchema("Moon", {});
    return true;
}();

Moon::Moon()
    : Named<Moon, CelestialBody>("Moon", Color4(0.9f, 0.9f, 1.0f, 1.0f)) {}

void Moon::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Moon", name, value)) return;
    CelestialBody::setProperty(name, value);
}

std::shared_ptr<Instance> Moon::clone() const {
    auto copy = std::make_shared<Moon>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Moon");
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
    return copy;
}
