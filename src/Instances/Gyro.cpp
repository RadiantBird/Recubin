#include <Instances/Gyro.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Workspace.hpp>
#include <Core/Physics.hpp>
#include <Core/PropertyRegistry.hpp>
#include <cmath>

namespace {
PropertyDesc partProperty() {
    auto property = PropertyRegistry::custom("Part", PropType::String,
        [](Instance* object) { return PropValue(static_cast<Gyro*>(object)->m_cube0Name); },
        [](Instance* object, const PropValue& value) {
            YAML::Node node(std::get<std::string>(value));
            static_cast<Gyro*>(object)->setProperty("Part", node);
        });
    property.omitEmpty();
    property.instanceRefClass = "BaseCube";
    property.editorWidget = EditorWidget::InstanceReference;
    return property;
}

void updateGyro(Gyro* self) {
    if (auto* workspace = self->findFirstAncestorWorkspace())
        if (auto* physics = static_cast<Workspace*>(workspace)->getPhysicsEngine())
            physics->updateConstraint(self->shared_from_this());
}

const bool s_gyroRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Gyro", "PhysicsConstraint", {
        partProperty(),
        custom("TargetRotation", PropType::Quaternion, [](Instance* o) { return PropValue(static_cast<Gyro*>(o)->TargetRotation); }, [](Instance* o, const PropValue& v) { static_cast<Gyro*>(o)->setTargetRotation(std::get<Quaternion>(v)); }),
        custom("Frequency", PropType::Float, [](Instance* o) { return PropValue(static_cast<Gyro*>(o)->Frequency); }, [](Instance* o, const PropValue& v) { static_cast<Gyro*>(o)->setFrequency(std::get<float>(v)); }),
        custom("DampingRatio", PropType::Float, [](Instance* o) { return PropValue(static_cast<Gyro*>(o)->DampingRatio); }, [](Instance* o, const PropValue& v) { static_cast<Gyro*>(o)->setDampingRatio(std::get<float>(v)); }),
        custom("MaxTorque", PropType::Float, [](Instance* o) { return PropValue(static_cast<Gyro*>(o)->MaxTorque); }, [](Instance* o, const PropValue& v) { static_cast<Gyro*>(o)->setMaxTorque(std::get<float>(v)); }),
    });
    return true;
}();
}

Gyro::Gyro() : PhysicsConstraint("Gyro") {}
std::shared_ptr<BaseCube> Gyro::getPart() const { return getCube0(); }
void Gyro::setPart(std::shared_ptr<BaseCube> part) { setCubes(part, part); }
void Gyro::setTargetRotation(Quaternion value) { if (value.tryNormalize()) { TargetRotation = value; updateGyro(this); } }
void Gyro::setFrequency(float value) { if (std::isfinite(value) && value >= 0.0f) { Frequency = value; updateGyro(this); } }
void Gyro::setDampingRatio(float value) { if (std::isfinite(value) && value >= 0.0f) { DampingRatio = value; updateGyro(this); } }
void Gyro::setMaxTorque(float value) { if (std::isfinite(value) && value >= 0.0f) { MaxTorque = value; updateGyro(this); } }
std::string Gyro::getClassName() { return "Gyro"; }
bool Gyro::IsA(std::string className) { return className == "Gyro" || PhysicsConstraint::IsA(className); }

void Gyro::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Part") {
        m_cube0Name = value.as<std::string>(); m_cube1Name = m_cube0Name;
        m_cube0.reset(); m_cube1.reset(); invalidateBinding(); resolveReferencesAndRegister(); return;
    }
    if (PropertyRegistry::loadProperty(this, "Gyro", name, value)) return;
    PhysicsConstraint::setProperty(name, value);
}

std::shared_ptr<Instance> Gyro::clone() const {
    auto copy = std::make_shared<Gyro>();
    copy->Name = Name; copy->Enabled = Enabled;
    copy->m_cube0Name = m_cube0Name; copy->m_cube1Name = m_cube1Name;
    copy->m_cube0 = m_cube0; copy->m_cube1 = m_cube1;
    PropertyRegistry::cloneFields(this, copy.get(), "Gyro");
    for (auto const& [name, child] : children) copy->addChild(child->clone());
    return copy;
}

void Gyro::remapClonedInstances(const CloneRemap& map) {
    if (auto part = m_cube0.lock()) if (auto it = map.find(part.get()); it != map.end()) setPart(std::static_pointer_cast<BaseCube>(it->second));
}

void Gyro::collectInstanceReferences(std::vector<InstanceReference>& out) {
    auto self = this;
    out.push_back({m_cube0.lock(), "BaseCube", "Gyro.Part", [self](std::shared_ptr<Instance> value) { self->setPart(std::dynamic_pointer_cast<BaseCube>(value)); }});
}
