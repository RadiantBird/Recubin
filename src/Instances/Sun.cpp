#include <Instances/Sun.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>
#include <cmath>

namespace {

constexpr float DEGREES_TO_RADIANS = 3.14159265358979f / 180.0f;
constexpr float RADIANS_TO_DEGREES = 180.0f / 3.14159265358979f;

bool finiteAngle(const Vector2& angle) {
    return std::isfinite(angle.x) && std::isfinite(angle.y);
}

} // namespace

// Angle: 非有限値は拒否する。旧形式のスカラーAngleもここで読み替える。
static const bool s_sunRegistered = []{
    using namespace PropertyRegistry;

    PropertyDesc angle = custom("Angle", PropType::Vec2,
        [](Instance* o) { return PropValue(static_cast<Sun*>(o)->Angle); },
        [](Instance* o, const PropValue& v) {
            static_cast<Sun*>(o)->setAngle(std::get<Vector2>(v));
        });
    angle.yamlReadWith([](Instance* o, const YAML::Node& value) {
        auto* sun = static_cast<Sun*>(o);
        if (value.IsScalar()) {
            // 旧形式: Z軸まわりの1平面を回るスカラー角
            sun->setAngle(Sun::angleFromLegacyScalar(value.as<float>(0.0f)));
            return true;
        }
        if (value.IsSequence() && value.size() == 2) {
            sun->setAngle(Vector2(value[0].as<float>(0.0f), value[1].as<float>(0.0f)));
            return true;
        }
        RCBN_ERROR("Sun '" << sun->Name << "' has an invalid Angle; expected [azimuth, elevation]");
        return true;
    });

    CelestialBody::registerSchema("Sun", { angle });
    return true;
}();

Sun::Sun()
    : Named<Sun, CelestialBody>("Sun", Color4(1.0f, 0.95f, 0.8f, 1.0f)) {}

void Sun::setAngle(const Vector2& angle) {
    if (!finiteAngle(angle)) {
        RCBN_ERROR("Rejected non-finite Angle for Sun " << Name);
        return;
    }
    Angle = angle;
}

void Sun::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Sun", name, value)) return;
    CelestialBody::setProperty(name, value);
}

std::shared_ptr<Instance> Sun::clone() const {
    auto copy = std::make_shared<Sun>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Sun");
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
    return copy;
}

Vector2 Sun::defaultAngle() {
    // 太陽方向 (-1,1,1)/√3: 方角 atan2(1,-1)=135°、高度 asin(1/√3)
    return Vector2(135.0f, std::asin(1.0f / std::sqrt(3.0f)) * RADIANS_TO_DEGREES);
}

Vector3 Sun::directionFromAngle(const Vector2& angle) {
    const Vector2 safe = finiteAngle(angle) ? angle : defaultAngle();
    const float elevation = std::fmax(-90.0f, std::fmin(90.0f, safe.y)) * DEGREES_TO_RADIANS;
    const float azimuth = safe.x * DEGREES_TO_RADIANS;
    const float horizontal = std::cos(elevation);
    return Vector3(horizontal * std::cos(azimuth), std::sin(elevation), horizontal * std::sin(azimuth));
}

Vector3 Sun::lightDirectionFromAngle(const Vector2& angle) {
    return -directionFromAngle(angle);
}

Vector2 Sun::angleFromDirection(const Vector3& direction) {
    const float length = direction.length();
    if (!std::isfinite(length) || length < 1.0e-6f) return defaultAngle();
    float azimuth = std::atan2(direction.z, direction.x) * RADIANS_TO_DEGREES;
    if (azimuth < 0.0f) azimuth += 360.0f;
    const float sine = std::fmax(-1.0f, std::fmin(1.0f, direction.y / length));
    return Vector2(azimuth, std::asin(sine) * RADIANS_TO_DEGREES);
}

Vector2 Sun::angleFromLegacyScalar(float degrees) {
    const float radians = degrees * DEGREES_TO_RADIANS;
    return angleFromDirection(Vector3(0.0f, std::sin(radians), std::cos(radians)));
}
