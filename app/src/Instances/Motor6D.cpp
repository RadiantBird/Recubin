#include <Instances/Motor6D.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Workspace.hpp>
#include <Core/Physics.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>

#include <cmath>
#include <utility>

namespace {

PropertyDesc partProperty(const char* name, bool first) {
    auto property = PropertyRegistry::custom(
        name,
        PropType::String,
        [first](Instance* object) {
            auto* motor = static_cast<Motor6D*>(object);
            return PropValue(first ? motor->m_cube0Name : motor->m_cube1Name);
        },
        [name](Instance* object, const PropValue& value) {
            YAML::Node node(std::get<std::string>(value));
            static_cast<Motor6D*>(object)->setProperty(name, node);
        }
    );

    property.omitEmpty();
    property.noClone();
    
    property.instanceRefClass = "BaseCube";
    property.editorWidget = EditorWidget::InstanceReference;
    return property;
}

template<typename T, T Motor6D::* Member, void (Motor6D::* Setter)(T)>
PropertyDesc motorProperty(const char* name, PropType type) {
    return PropertyRegistry::custom(
        name,
        type,
        [](Instance* object) {
            auto* motor = static_cast<Motor6D*>(object);
            return PropValue(motor->*Member);
        },
        [](Instance* object, const PropValue& value) {
            auto* motor = static_cast<Motor6D*>(object);
            (motor->*Setter)(std::get<T>(value));
        }
    );
}

void updateMotor6D(Motor6D& motor) {
    auto* workspace = motor.findFirstAncestorWorkspace();
    if (!workspace) return; // Detached while constructing/loading is valid.

    auto* physics = static_cast<Workspace*>(workspace)->getPhysicsEngine();
    if (!physics) return; // Workspace may exist before Physics is ready.

    physics->updateConstraint(motor.shared_from_this());
}

void setPartReference(Motor6D& motor, bool first, const std::shared_ptr<Instance>& value) {
    if (!value) {
        if (first) motor.setPart0(nullptr);
        else       motor.setPart1(nullptr);
        return;
    }

    auto part = std::dynamic_pointer_cast<BaseCube>(value);
    if (!part) {
        RCBN_ERROR(
            "Motor6D \"" << motor.getFullPath() << "\": "
            << (first ? "Part0" : "Part1")
            << " must reference BaseCube, got " << value->getClassName()
        );
        return;
    }

    if (first) motor.setPart0(std::move(part));
    else       motor.setPart1(std::move(part));
}

const bool s_motor6DRegistered = [] {
    using namespace PropertyRegistry;

    registerClass("Motor6D", "PhysicsConstraint", {
        partProperty("Part0", true),
        partProperty("Part1", false),
        motorProperty<CFrame, &Motor6D::C0, &Motor6D::setC0>("C0", PropType::CFrame),
        motorProperty<CFrame, &Motor6D::C1, &Motor6D::setC1>("C1", PropType::CFrame),
        motorProperty<CFrame, &Motor6D::Transform, &Motor6D::setTransform>("Transform", PropType::CFrame),
        motorProperty<float, &Motor6D::Frequency, &Motor6D::setFrequency>("Frequency", PropType::Float),
        motorProperty<float, &Motor6D::DampingRatio, &Motor6D::setDampingRatio>("DampingRatio", PropType::Float),
    });

    return true;
}();

} // namespace

Motor6D::Motor6D()
    : PhysicsConstraint("Motor6D") {}

std::shared_ptr<BaseCube> Motor6D::getPart0() const { return getCube0(); }
std::shared_ptr<BaseCube> Motor6D::getPart1() const { return getCube1(); }

void Motor6D::setPart0(std::shared_ptr<BaseCube> part) { setCube0(std::move(part)); }
void Motor6D::setPart1(std::shared_ptr<BaseCube> part) { setCube1(std::move(part)); }

void Motor6D::setC0(CFrame value) {
    C0 = value;
    updateMotor6D(*this);
}

void Motor6D::setC1(CFrame value) {
    C1 = value;
    updateMotor6D(*this);
}

void Motor6D::setTransform(CFrame value) {
    Transform = value;
    updateMotor6D(*this);
}

void Motor6D::setFrequency(float value) {
    if (!std::isfinite(value) || value < 0.0f) {
        RCBN_WARN("Motor6D \"" << getFullPath() << "\": invalid Frequency=" << value);
        return;
    }

    Frequency = value;
    updateMotor6D(*this);
}

void Motor6D::setDampingRatio(float value) {
    if (!std::isfinite(value) || value < 0.0f) {
        RCBN_WARN("Motor6D \"" << getFullPath() << "\": invalid DampingRatio=" << value);
        return;
    }

    DampingRatio = value;
    updateMotor6D(*this);
}

std::string Motor6D::getClassName() {
    return "Motor6D";
}

bool Motor6D::IsA(std::string className) {
    return className == "Motor6D" || PhysicsConstraint::IsA(className);
}

void Motor6D::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Part0" || name == "Part1") {
        std::string& referenceName = name == "Part0" ? m_cube0Name : m_cube1Name;
        auto& reference = name == "Part0" ? m_cube0 : m_cube1;

        referenceName = value.as<std::string>();
        reference.reset();
        invalidateBinding();
        resolveReferencesAndRegister();
        return;
    }

    if (PropertyRegistry::loadProperty(this, "Motor6D", name, value)) return;
    PhysicsConstraint::setProperty(name, value);
}

std::shared_ptr<Instance> Motor6D::clone() const {
    auto copy = std::make_shared<Motor6D>();
    copy->Name = Name;
    copy->Enabled = Enabled;
    copy->m_cube0Name = m_cube0Name;
    copy->m_cube1Name = m_cube1Name;
    copy->m_cube0 = m_cube0;
    copy->m_cube1 = m_cube1;

    PropertyRegistry::cloneFields(this, copy.get(), "Motor6D");

    for (const auto& child : children)
        copy->addChild(child.second->clone());

    return copy;
}

void Motor6D::remapClonedInstances(const CloneRemap& map) {
    auto remap = [&map](std::weak_ptr<BaseCube>& reference) {
        auto part = reference.lock();
        if (!part) return;

        auto it = map.find(part.get());
        if (it != map.end())
            reference = std::static_pointer_cast<BaseCube>(it->second);
    };

    remap(m_cube0);
    remap(m_cube1);
    refreshRefNames();
}

void Motor6D::collectInstanceReferences(std::vector<InstanceReference>& out) {
    auto* self = this;

    out.push_back({
        m_cube0.lock(), "BaseCube", "Motor6D.Part0",
        [self](std::shared_ptr<Instance> value) { setPartReference(*self, true, value); }
    });

    out.push_back({
        m_cube1.lock(), "BaseCube", "Motor6D.Part1",
        [self](std::shared_ptr<Instance> value) { setPartReference(*self, false, value); }
    });
}
