#include <include/Instances/Lighting.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>
#include <cmath>

// プロパティ・スキーマ（単一の正）。Luau/YAML/clone/エディターを一括駆動。
static const bool s_lightingRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Lighting", {
        field<&Lighting::brightness>("Brightness",  0.0f, 5.0f, 0.01f).clampLua(),
        field<&Lighting::lightColor>("Color"),
        field<&Lighting::shadowDistance>("ShadowDistance", 0.0f, 10000.0f, 1.0f).clampLua(),
        field<&Lighting::shadowFadeDistance>("ShadowFadeDistance", 0.0f, 10000.0f, 1.0f).clampLua(),
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
    if (name == "Direction") {
        // 廃止されたプロパティ。向きはSunが持つので、旧形式の値は引き継ぎ用に保持するだけ。
        if (value.IsSequence() && value.size() == 3) {
            const Vector3 direction(value[0].as<float>(0.0f), value[1].as<float>(0.0f),
                                    value[2].as<float>(0.0f));
            if (std::isfinite(direction.length()) && direction.length() > 0.001f)
                legacyDirection = direction;
            else
                RCBN_WARN("Lighting '" << Name << "' has an invalid legacy Direction; ignoring it");
        } else {
            RCBN_WARN("Lighting '" << Name << "' has a malformed legacy Direction; ignoring it");
        }
        return;
    }
    if (PropertyRegistry::loadProperty(this, "Lighting", name, value)) return;
    Instance::setProperty(name, value);
}
