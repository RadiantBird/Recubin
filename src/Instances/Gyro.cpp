#include <Instances/Gyro.hpp>

#include <Instances/BaseCube.hpp>
#include <Instances/Workspace.hpp>
#include <Core/Physics.hpp>
#include <Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>

#include <cmath>
#include <numbers>
#include <utility>

namespace {

PropertyDesc partProperty() {
    auto property = PropertyRegistry::custom(
        "Part",
        PropType::String,
        [](Instance* object) {
            auto* gyro = static_cast<Gyro*>(object);
            return PropValue(gyro->m_cube0Name);
        },
        [](Instance* object, const PropValue& value) {
            auto* gyro = static_cast<Gyro*>(object);
            YAML::Node node(std::get<std::string>(value));
            gyro->setProperty("Part", node);
        }
    );

    property.omitEmpty();
    property.instanceRefClass = "BaseCube";
    property.editorWidget = EditorWidget::InstanceReference;

    return property;
}

PropertyDesc axisEnabledProperty(
    const char* name,
    GyroAxis axis
) {
    return PropertyRegistry::custom(
        name,
        PropType::Bool,
        [axis](Instance* object) {
            auto* gyro = static_cast<Gyro*>(object);
            return PropValue(
                gyro->getAxisSettings(axis).Enabled
            );
        },
        [axis](Instance* object, const PropValue& value) {
            auto* gyro = static_cast<Gyro*>(object);

            gyro->setAxisEnabled(
                axis,
                std::get<bool>(value)
            );
        }
    );
}

PropertyDesc targetAngleProperty(
    const char* name,
    GyroAxis axis
) {
    return PropertyRegistry::custom(
        name,
        PropType::Float,
        [axis](Instance* object) {
            auto* gyro = static_cast<Gyro*>(object);
            return PropValue(
                gyro->getAxisSettings(axis).TargetAngle
            );
        },
        [axis](Instance* object, const PropValue& value) {
            auto* gyro = static_cast<Gyro*>(object);

            gyro->setTargetAngle(
                axis,
                std::get<float>(value)
            );
        }
    );
}

PropertyDesc maxTorqueProperty(
    const char* name,
    GyroAxis axis
) {
    return PropertyRegistry::custom(
        name,
        PropType::Float,
        [axis](Instance* object) {
            auto* gyro = static_cast<Gyro*>(object);
            return PropValue(
                gyro->getAxisSettings(axis).MaxTorque
            );
        },
        [axis](Instance* object, const PropValue& value) {
            auto* gyro = static_cast<Gyro*>(object);

            gyro->setMaxTorque(
                axis,
                std::get<float>(value)
            );
        }
    );
}

PropertyDesc maxAngularSpeedProperty(
    const char* name,
    GyroAxis axis
) {
    return PropertyRegistry::custom(
        name,
        PropType::Float,
        [axis](Instance* object) {
            auto* gyro = static_cast<Gyro*>(object);
            return PropValue(
                gyro->getAxisSettings(axis).MaxAngularSpeed
            );
        },
        [axis](Instance* object, const PropValue& value) {
            auto* gyro = static_cast<Gyro*>(object);

            gyro->setMaxAngularSpeed(
                axis,
                std::get<float>(value)
            );
        }
    );
}

bool registerGyroProperties() {
    using namespace PropertyRegistry;

    // @RadiantBird 2026/09/12:
    // Keep this registration readable.
    // Do not collapse it into an immediately-invoked lambda or giant one-liners.
    registerClass(
        "Gyro",
        "PhysicsConstraint",
        {
            partProperty(),

            axisEnabledProperty(
                "XEnabled",
                GyroAxis::X
            ),
            targetAngleProperty(
                "XTargetAngle",
                GyroAxis::X
            ),
            maxTorqueProperty(
                "XMaxTorque",
                GyroAxis::X
            ),
            maxAngularSpeedProperty(
                "XMaxAngularSpeed",
                GyroAxis::X
            ),

            axisEnabledProperty(
                "YEnabled",
                GyroAxis::Y
            ),
            targetAngleProperty(
                "YTargetAngle",
                GyroAxis::Y
            ),
            maxTorqueProperty(
                "YMaxTorque",
                GyroAxis::Y
            ),
            maxAngularSpeedProperty(
                "YMaxAngularSpeed",
                GyroAxis::Y
            ),

            axisEnabledProperty(
                "ZEnabled",
                GyroAxis::Z
            ),
            targetAngleProperty(
                "ZTargetAngle",
                GyroAxis::Z
            ),
            maxTorqueProperty(
                "ZMaxTorque",
                GyroAxis::Z
            ),
            maxAngularSpeedProperty(
                "ZMaxAngularSpeed",
                GyroAxis::Z
            ),
        }
    );

    return true;
}

// The project currently auto-registers PropertyRegistry classes at static init.
// Keep the side effect named and visible instead of hiding it in an IIFE lambda.
[[maybe_unused]]
const bool s_gyroRegistered = registerGyroProperties();

} // namespace

namespace {

constexpr float RADIANS_TO_DEGREES =
    180.0f / std::numbers::pi_v<float>;

float degreesFromAtan2(float y, float x) {
    return std::atan2(y, x) * RADIANS_TO_DEGREES;
}

} // namespace

Gyro::Gyro()
    : PhysicsConstraint("Gyro") {
}

bool Gyro::endpointsReady() const {
    return !m_cube0.expired();
}

GyroAxisSettings& Gyro::axisSettings(GyroAxis axis) {
    switch (axis) {
    case GyroAxis::X:
        return m_xAxis;

    case GyroAxis::Y:
        return m_yAxis;

    case GyroAxis::Z:
        return m_zAxis;
    }

    std::unreachable();
}

const GyroAxisSettings& Gyro::axisSettings(GyroAxis axis) const {
    switch (axis) {
    case GyroAxis::X:
        return m_xAxis;

    case GyroAxis::Y:
        return m_yAxis;

    case GyroAxis::Z:
        return m_zAxis;
    }

    std::unreachable();
}

void Gyro::notifyChanged() {
    auto* workspace = findFirstAncestorWorkspace();

    if (!workspace) {
        return;
    }

    auto* physics =
        static_cast<Workspace*>(workspace)->getPhysicsEngine();

    if (!physics) {
        return;
    }

    physics->updateConstraint(
        shared_from_this()
    );
}

std::shared_ptr<BaseCube> Gyro::getPart() const {
    return m_cube0.lock();
}

void Gyro::setPart(std::shared_ptr<BaseCube> part) {
    invalidateBinding();

    m_cube0 = part;
    m_cube0Name =
        part
        ? part->getWorkspaceRelativePath()
        : std::string{};

    // @RadiantBird 2026/09/12:
    // Gyro has exactly one physical endpoint.
    // Do not mirror Part into Cube1 merely to satisfy PhysicsConstraint.
    m_cube1.reset();
    m_cube1Name.clear();

    registerIfReady();
}

const GyroAxisSettings& Gyro::getAxisSettings(
    GyroAxis axis
) const {
    return axisSettings(axis);
}

float Gyro::headingAngleFromDirection(
    const Vector3& direction
) {
    return degreesFromAtan2(
        -direction.x,
        -direction.z
    );
}

float Gyro::angleFromRotation(
    GyroAxis axis,
    const Quaternion& rotation
) {
    const Vector3 up = rotation.getUp();

    switch (axis) {
    case GyroAxis::X:
        return degreesFromAtan2(up.z, up.y);

    case GyroAxis::Z:
        return degreesFromAtan2(-up.x, up.y);

    case GyroAxis::Y: {
        const Vector3 forward = rotation.getForward();
        const float forwardHorizontalLengthSquared =
            forward.x * forward.x + forward.z * forward.z;

        if (forwardHorizontalLengthSquared > 1e-8f) {
            return headingAngleFromDirection(forward);
        }

        // @RadiantBird 2026/09/12:
        // A vertical forward vector has no usable horizontal heading. The
        // right vector still preserves yaw at this singular orientation.
        const Vector3 right = rotation.getRight();
        return degreesFromAtan2(-right.z, right.x);
    }
    }

    std::unreachable();
}

void Gyro::setCharacterHeading(
    const Vector3& direction
) {
    if (
        !std::isfinite(direction.x) ||
        !std::isfinite(direction.y) ||
        !std::isfinite(direction.z)
    ) {
        RCBN_ERROR(
            "Rejected non-finite Gyro character heading in "
            << getFullPath()
        );
        return;
    }

    const float horizontalLengthSquared =
        direction.x * direction.x +
        direction.z * direction.z;
    if (horizontalLengthSquared <= 1e-8f) {
        RCBN_ERROR(
            "Rejected Gyro character heading without a horizontal direction in "
            << getFullPath()
        );
        return;
    }

    setTargetAngle(
        GyroAxis::Y,
        headingAngleFromDirection(direction)
    );
}

void Gyro::setCharacterRotation(
    const Quaternion& rotation
) {
    if (!rotation.isNormalized()) {
        RCBN_ERROR(
            "Rejected invalid Gyro character rotation in "
            << getFullPath()
        );
        return;
    }

    setTargetAngle(
        GyroAxis::Y,
        angleFromRotation(GyroAxis::Y, rotation)
    );
}

void Gyro::setAxisEnabled(
    GyroAxis axis,
    bool enabled
) {
    auto& settings = axisSettings(axis);

    if (settings.Enabled == enabled) {
        return;
    }

    settings.Enabled = enabled;
    notifyChanged();
}

void Gyro::setTargetAngle(
    GyroAxis axis,
    float angle
) {
    if (!std::isfinite(angle)) {
        RCBN_ERROR(
            "Rejected non-finite Gyro target angle in "
            << getFullPath()
        );
        return;
    }

    auto& settings = axisSettings(axis);

    if (settings.TargetAngle == angle) {
        return;
    }

    settings.TargetAngle = angle;
    notifyChanged();
}

void Gyro::setMaxTorque(
    GyroAxis axis,
    float torque
) {
    if (!std::isfinite(torque) || torque < 0.0f) {
        RCBN_ERROR(
            "Rejected invalid Gyro MaxTorque="
            << torque
            << " in "
            << getFullPath()
        );
        return;
    }

    auto& settings = axisSettings(axis);

    if (settings.MaxTorque == torque) {
        return;
    }

    settings.MaxTorque = torque;
    notifyChanged();
}

void Gyro::setMaxAngularSpeed(
    GyroAxis axis,
    float speed
) {
    if (!std::isfinite(speed) || speed < 0.0f) {
        RCBN_ERROR(
            "Rejected invalid Gyro MaxAngularSpeed="
            << speed
            << " in "
            << getFullPath()
        );
        return;
    }

    auto& settings = axisSettings(axis);

    if (settings.MaxAngularSpeed == speed) {
        return;
    }

    settings.MaxAngularSpeed = speed;
    notifyChanged();
}

std::string Gyro::getClassName() {
    return "Gyro";
}

bool Gyro::IsA(std::string className) {
    if (className == "Gyro") {
        return true;
    }

    return PhysicsConstraint::IsA(
        std::move(className)
    );
}

void Gyro::setProperty(
    const std::string& name,
    const YAML::Node& value
) {
    if (name == "Part") {
        invalidateBinding();

        m_cube0Name = value.as<std::string>();
        m_cube0.reset();

        m_cube1Name.clear();
        m_cube1.reset();

        resolveReferencesAndRegister();
        return;
    }

    if (
        name == "TargetRotation" ||
        name == "Frequency" ||
        name == "DampingRatio"
    ) {
        RCBN_ERROR(
            "Obsolete Gyro property '"
            << name
            << "' found in "
            << getFullPath()
            << ". Gyro now uses per-axis X/Y/Z TargetAngle/MaxTorque/MaxAngularSpeed."
        );
        return;
    }

    if (
        PropertyRegistry::loadProperty(
            this,
            "Gyro",
            name,
            value
        )
    ) {
        return;
    }

    PhysicsConstraint::setProperty(
        name,
        value
    );
}

std::shared_ptr<Instance> Gyro::clone() const {
    auto copy = std::make_shared<Gyro>();

    copy->Name = Name;
    copy->Enabled = Enabled;

    copy->m_cube0Name = m_cube0Name;
    copy->m_cube0 = m_cube0;

    copy->m_xAxis = m_xAxis;
    copy->m_yAxis = m_yAxis;
    copy->m_zAxis = m_zAxis;

    for (const auto& [name, child] : children) {
        copy->addChild(
            child->clone()
        );
    }

    return copy;
}

void Gyro::remapClonedInstances(
    const CloneRemap& map
) {
    auto part = m_cube0.lock();

    if (!part) {
        RCBN_ERROR(
            "Gyro clone remap has unresolved Part in "
            << getFullPath()
        );
        return;
    }

    auto iterator = map.find(
        part.get()
    );

    // Not in the map means this is an external reference.
    // Preserve it instead of treating it as an internal clone failure.
    if (iterator == map.end()) {
        return;
    }

    auto clonedPart =
        std::dynamic_pointer_cast<BaseCube>(
            iterator->second
        );

    if (!clonedPart) {
        RCBN_ERROR(
            "Gyro clone remap resolved Part to a non-BaseCube in "
            << getFullPath()
        );
        m_cube0.reset();
        return;
    }

    setPart(
        std::move(clonedPart)
    );
}

void Gyro::collectInstanceReferences(
    std::vector<InstanceReference>& out
) {
    auto self = this;

    out.push_back({
        m_cube0.lock(),
        "BaseCube",
        "Gyro.Part",
        [self](std::shared_ptr<Instance> value) {
            self->setPart(
                std::dynamic_pointer_cast<BaseCube>(
                    value
                )
            );
        }
    });
}
