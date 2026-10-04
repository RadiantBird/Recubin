#include <Instances/Moon.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <cmath>

static const bool s_moonRegistered = []{
    using namespace PropertyRegistry;
    CelestialBody::registerSchema("Moon", {
        field<&Moon::Phase>("Phase", 0.0f, 1.0f, 0.01f).group("Phase"),
        field<&Moon::PhaseRotation>("PhaseRotation", -360.0f, 360.0f, 1.0f),
        field<&Moon::Earthshine>("Earthshine", 0.0f, 1.0f, 0.01f).clampLua(),
    });
    return true;
}();

Moon::Moon()
    : Named<Moon, CelestialBody>("Moon", Color4(0.9f, 0.9f, 1.0f, 1.0f)) {}

float Moon::wrappedPhase() const {
    if (!std::isfinite(Phase)) return FULL_PHASE;
    return Phase - std::floor(Phase);
}

bool Moon::hasPhase() const {
    return std::fabs(wrappedPhase() - FULL_PHASE) > 0.001f;
}

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
