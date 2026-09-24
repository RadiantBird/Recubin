#include <include/Instances/BallSocket.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>
#include <cmath>
#include <utility>

namespace {

template<auto Field, auto Setter>
PropertyDesc ballSocketModeProperty(
    std::string_view name,
    const std::vector<std::pair<std::string_view, int>>& names) {
    PropertyDesc property = PropertyRegistry::custom(
        name, PropType::Enum,
        [](Instance* object) {
            return PropValue(static_cast<int>(
                static_cast<BallSocket*>(object)->*Field));
        },
        [](Instance* object, const PropValue& value) {
            (static_cast<BallSocket*>(object)->*Setter)(
                static_cast<BallSocketAngularMode>(std::get<int>(value)));
        });
    property.enumNames = names;
    property.yamlEnumAsString = true;
    return property;
}

template<auto Field, auto Setter>
PropertyDesc ballSocketAngleProperty(
    std::string_view name) {
    PropertyDesc property = PropertyRegistry::custom(
        name, PropType::Float,
        [](Instance* object) {
            return PropValue(
                static_cast<BallSocket*>(object)->*Field);
        },
        [](Instance* object, const PropValue& value) {
            (static_cast<BallSocket*>(object)->*Setter)(
                std::get<float>(value));
        });
    property.lo = -180.0f;
    property.hi = 180.0f;
    property.step = 1.0f;
    return property;
}

} // namespace

static const bool s_ballSocketRegistered = [] {
    using namespace PropertyRegistry;
    const std::vector<std::pair<std::string_view, int>> angularModes = {
        {"Free", static_cast<int>(BallSocketAngularMode::Free)},
        {"Limited", static_cast<int>(BallSocketAngularMode::Limited)},
        {"Locked", static_cast<int>(BallSocketAngularMode::Locked)},
    };
    registerClass("BallSocket", "PhysicsConstraint", {
        instanceRefProperty<&PhysicsConstraint::m_cube0Name>("Cube0", "BaseCube"),
        instanceRefProperty<&PhysicsConstraint::m_cube1Name>("Cube1", "BaseCube"),
        instanceRefProperty<&BallSocket::m_attachment0Name>("Attachment0", "Attachment").omitEmpty(),
        instanceRefProperty<&BallSocket::m_attachment1Name>("Attachment1", "Attachment").omitEmpty(),
        ballSocketModeProperty<&BallSocket::AngularXMode, &BallSocket::setAngularXMode>("AngularXMode", angularModes),
        ballSocketAngleProperty<&BallSocket::AngularXMin, &BallSocket::setAngularXMin>("AngularXMin"),
        ballSocketAngleProperty<&BallSocket::AngularXMax, &BallSocket::setAngularXMax>("AngularXMax"),
        ballSocketModeProperty<&BallSocket::AngularYMode, &BallSocket::setAngularYMode>("AngularYMode", angularModes),
        ballSocketAngleProperty<&BallSocket::AngularYMin, &BallSocket::setAngularYMin>("AngularYMin"),
        ballSocketAngleProperty<&BallSocket::AngularYMax, &BallSocket::setAngularYMax>("AngularYMax"),
        ballSocketModeProperty<&BallSocket::AngularZMode, &BallSocket::setAngularZMode>("AngularZMode", angularModes),
        ballSocketAngleProperty<&BallSocket::AngularZMin, &BallSocket::setAngularZMin>("AngularZMin"),
        ballSocketAngleProperty<&BallSocket::AngularZMax, &BallSocket::setAngularZMax>("AngularZMax"),
    });
    return true;
}();

BallSocket::BallSocket()
    : PhysicsConstraint("BallSocket") {}

BallSocket::BallSocket(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : PhysicsConstraint("BallSocket") {
    setCubes(std::move(cube0), std::move(cube1));
}

void BallSocket::refreshRefNames() {
    PhysicsConstraint::refreshRefNames();
    if (auto c0 = m_cube0.lock(); c0 && !m_cube0Name.empty())
        m_cube0Name = c0->getWorkspaceRelativePath();
    if (auto c1 = m_cube1.lock(); c1 && !m_cube1Name.empty())
        m_cube1Name = c1->getWorkspaceRelativePath();
    if (auto a0 = m_attachment0.lock(); a0 && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0Name = a0->getPathUpTo(c0.get());
    if (auto a1 = m_attachment1.lock(); a1 && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1Name = a1->getPathUpTo(c1.get());
}

void BallSocket::resolveAdditionalReferences() {
    if (!m_attachment0.lock() && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0 = Attachment::findUnder(c0.get(), m_attachment0Name);
    if (!m_attachment1.lock() && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1 = Attachment::findUnder(c1.get(), m_attachment1Name);
}

namespace {

bool isValidBallSocketAngularMode(BallSocketAngularMode mode) {
    return mode == BallSocketAngularMode::Free ||
           mode == BallSocketAngularMode::Limited ||
           mode == BallSocketAngularMode::Locked;
}

void reportInvalidBallSocketMode(const BallSocket& socket, const char* property,
                                 BallSocketAngularMode mode) {
    RCBN_WARN("BallSocket \"" << socket.getFullPath() << "\": " << property
              << " has invalid mode value " << static_cast<int>(mode)
              << "; keeping the previous value");
}

void reportInvalidBallSocketAngle(const BallSocket& socket, const char* property,
                                  float value) {
    RCBN_WARN("BallSocket \"" << socket.getFullPath() << "\": " << property
              << " must be finite; keeping the previous value (received "
              << value << ")");
}

} // namespace

void BallSocket::refreshAngularBinding() {
    invalidateBinding();
    registerIfReady();
}

void BallSocket::setAngularXMode(BallSocketAngularMode mode) {
    if (!isValidBallSocketAngularMode(mode)) {
        reportInvalidBallSocketMode(*this, "AngularXMode", mode);
        return;
    }
    if (AngularXMode == mode) return;
    AngularXMode = mode;
    refreshAngularBinding();
}

void BallSocket::setAngularYMode(BallSocketAngularMode mode) {
    if (!isValidBallSocketAngularMode(mode)) {
        reportInvalidBallSocketMode(*this, "AngularYMode", mode);
        return;
    }
    if (AngularYMode == mode) return;
    AngularYMode = mode;
    refreshAngularBinding();
}

void BallSocket::setAngularZMode(BallSocketAngularMode mode) {
    if (!isValidBallSocketAngularMode(mode)) {
        reportInvalidBallSocketMode(*this, "AngularZMode", mode);
        return;
    }
    if (AngularZMode == mode) return;
    AngularZMode = mode;
    refreshAngularBinding();
}

void BallSocket::setAngularXMin(float angle) {
    if (!std::isfinite(angle)) {
        reportInvalidBallSocketAngle(*this, "AngularXMin", angle);
        return;
    }
    if (AngularXMin == angle) return;
    AngularXMin = angle;
    refreshAngularBinding();
}

void BallSocket::setAngularXMax(float angle) {
    if (!std::isfinite(angle)) {
        reportInvalidBallSocketAngle(*this, "AngularXMax", angle);
        return;
    }
    if (AngularXMax == angle) return;
    AngularXMax = angle;
    refreshAngularBinding();
}

void BallSocket::setAngularYMin(float angle) {
    if (!std::isfinite(angle)) {
        reportInvalidBallSocketAngle(*this, "AngularYMin", angle);
        return;
    }
    if (AngularYMin == angle) return;
    AngularYMin = angle;
    refreshAngularBinding();
}

void BallSocket::setAngularYMax(float angle) {
    if (!std::isfinite(angle)) {
        reportInvalidBallSocketAngle(*this, "AngularYMax", angle);
        return;
    }
    if (AngularYMax == angle) return;
    AngularYMax = angle;
    refreshAngularBinding();
}

void BallSocket::setAngularZMin(float angle) {
    if (!std::isfinite(angle)) {
        reportInvalidBallSocketAngle(*this, "AngularZMin", angle);
        return;
    }
    if (AngularZMin == angle) return;
    AngularZMin = angle;
    refreshAngularBinding();
}

void BallSocket::setAngularZMax(float angle) {
    if (!std::isfinite(angle)) {
        reportInvalidBallSocketAngle(*this, "AngularZMax", angle);
        return;
    }
    if (AngularZMax == angle) return;
    AngularZMax = angle;
    refreshAngularBinding();
}

std::shared_ptr<Instance> BallSocket::clone() const {
    auto c = std::make_shared<BallSocket>();
    c->Name        = Name;
    PropertyRegistry::cloneFields(this, c.get(), "BallSocket");
    c->m_cube0     = m_cube0;
    c->m_cube1     = m_cube1;
    c->m_attachment0 = m_attachment0;
    c->m_attachment1 = m_attachment1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void BallSocket::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto a0 = m_attachment0.lock()) { auto it = map.find(a0.get()); if (it != map.end()) m_attachment0 = std::static_pointer_cast<Attachment>(it->second); }
    if (auto a1 = m_attachment1.lock()) { auto it = map.find(a1.get()); if (it != map.end()) m_attachment1 = std::static_pointer_cast<Attachment>(it->second); }
    refreshRefNames();
}

std::string BallSocket::getClassName() { return "BallSocket"; }

bool BallSocket::IsA(std::string className) {
    if (className == "BallSocket") return true;
    return PhysicsConstraint::IsA(className);
}

void BallSocket::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Cube0") {
        m_cube0Name = value.as<std::string>();
        m_cube0.reset();
        if (auto* ws_raw = findFirstAncestorWorkspace()) {
            auto* child = ws_raw->getChildByPath(m_cube0Name);
            if (child && child->IsA("BaseCube"))
                m_cube0 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
        }
    } else if (name == "Cube1") {
        m_cube1Name = value.as<std::string>();
        m_cube1.reset();
        if (auto* ws_raw = findFirstAncestorWorkspace()) {
            auto* child = ws_raw->getChildByPath(m_cube1Name);
            if (child && child->IsA("BaseCube"))
                m_cube1 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
        }
    } else if (name == "Attachment0") {
        m_attachment0Name = value.as<std::string>();
        m_attachment0.reset(); // 名前変更後に registerIfReady() 経由で再解決させる
    } else if (name == "Attachment1") {
        m_attachment1Name = value.as<std::string>();
        m_attachment1.reset();
    } else {
        if (PropertyRegistry::loadProperty(this, "BallSocket", name, value))
            return;
        PhysicsConstraint::setProperty(name, value);
    }
    registerIfReady();
}
