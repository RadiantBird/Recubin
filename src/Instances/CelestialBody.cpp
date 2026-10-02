#include <include/Instances/CelestialBody.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace {

// BaseCube派生だった頃のSun/Moonが保存していたプロパティ。新形式では不要なので読み込み時に無視する。
constexpr std::array<std::string_view, 19> kLegacyBaseCubeKeys = {
    "Position", "Rotation", "CFrame", "Anchored", "CanCollide", "CanTouch", "Locked",
    "LockFlags", "CastShadow", "ShadowMode", "Unlit", "UseTriplanar", "TextureScale",
    "MassDensity", "CCDMode", "MaterialType", "StaticFriction", "DynamicFriction",
    "Restitution",
};

bool isLegacyBaseCubeKey(const std::string& name) {
    if (name == "Material") return true;
    return std::find(kLegacyBaseCubeKeys.begin(), kLegacyBaseCubeKeys.end(), name) !=
           kLegacyBaseCubeKeys.end();
}

} // namespace

CelestialBody::CelestialBody(std::string name, const Color4& color)
    : Instance(std::move(name)), Color(color) {}

void CelestialBody::registerSchema(std::string_view className, std::vector<PropertyDesc> leading) {
    using namespace PropertyRegistry;
    std::vector<PropertyDesc> props = std::move(leading);
    props.push_back(field<&CelestialBody::Color>("Color"));
    props.push_back(field<&CelestialBody::Distance>("Distance", MIN_DISTANCE, MAX_DISTANCE, 1.0f).clampLua());
    // 逆光
    props.push_back(field<&CelestialBody::GlowIntensity>("GlowIntensity", 0.0f, 4.0f, 0.01f)
        .clampLua().group("Backlight"));
    props.push_back(field<&CelestialBody::GlowRadius>("GlowRadius", 1.0f, 60.0f, 0.1f).clampLua());
    props.push_back(field<&CelestialBody::VeilIntensity>("VeilIntensity", 0.0f, 1.0f, 0.01f).clampLua());
    props.push_back(field<&CelestialBody::VeilFalloff>("VeilFalloff", 1.0f, 64.0f, 0.1f).clampLua());
    // 光条
    props.push_back(field<&CelestialBody::SpikeIntensity>("SpikeIntensity", 0.0f, 4.0f, 0.01f)
        .clampLua().group("Spikes"));
    props.push_back(field<&CelestialBody::SpikeCount>("SpikeCount", 2.0f, 16.0f, 1.0f).clampLua());
    props.push_back(field<&CelestialBody::SpikeLength>("SpikeLength", 1.0f, 60.0f, 0.1f).clampLua());
    props.push_back(field<&CelestialBody::SpikeRotation>("SpikeRotation", -360.0f, 360.0f, 1.0f));
    props.push_back(field<&CelestialBody::HorizonFade>("HorizonFade").group("Effects"));
    registerClass(className, std::move(props));
}

bool CelestialBody::hasFlareEffect() const {
    return GlowIntensity > 0.0f || VeilIntensity > 0.0f || SpikeIntensity > 0.0f;
}

float CelestialBody::apparentDiameter() const {
    const float distance = std::isfinite(Distance)
        ? std::clamp(Distance, MIN_DISTANCE, MAX_DISTANCE)
        : DEFAULT_DISTANCE;
    return baseDiameter() * SKY_RENDER_DISTANCE / distance;
}

bool CelestialBody::loadLegacyProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Size") {
        // 旧Sizeは直径(stud)（Vector3またはfloat）。見かけの大きさを保つDistanceへ換算する。
        float size = 0.0f;
        if (value.IsSequence() && value.size() >= 1) size = value[0].as<float>(0.0f);
        else if (value.IsScalar()) size = value.as<float>(0.0f);
        if (std::isfinite(size) && size > 0.0f) {
            Distance = std::clamp(baseDiameter() * SKY_RENDER_DISTANCE / size,
                                  MIN_DISTANCE, MAX_DISTANCE);
        } else {
            RCBN_WARN(getClassName() << " '" << Name << "' has an invalid legacy Size; using Distance "
                      << DEFAULT_DISTANCE);
            Distance = DEFAULT_DISTANCE;
        }
        return true;
    }
    if (!isLegacyBaseCubeKey(name)) return false;
    if (!m_loggedLegacyProperties) {
        m_loggedLegacyProperties = true;
        RCBN_LOG(getClassName() << " '" << Name << "' is no longer a BaseCube; ignoring legacy property '"
                 << name << "' (and any other BaseCube-only properties)");
    }
    return true;
}

bool CelestialBody::IsA(std::string className) {
    if (className == "CelestialBody") return true;
    return Instance::IsA(className);
}

void CelestialBody::setProperty(const std::string& name, const YAML::Node& value) {
    if (loadLegacyProperty(name, value)) return;
    Instance::setProperty(name, value);
}
