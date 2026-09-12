#include <Instances/Motor6D.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Workspace.hpp>
#include <Core/Physics.hpp>
#include <Core/PropertyRegistry.hpp>
#include <cmath>

namespace {
PropertyDesc partProperty(const char* name, bool first) {
    auto property = PropertyRegistry::custom(name, PropType::String,
        [first](Instance* object) {
            auto* self = static_cast<Motor6D*>(object);
            return PropValue(first ? self->m_cube0Name : self->m_cube1Name);
        },
        [name](Instance* object, const PropValue& value) {
            YAML::Node node(std::get<std::string>(value));
            static_cast<Motor6D*>(object)->setProperty(name, node);
        });
    property.omitEmpty();
    property.instanceRefClass = "BaseCube";
    property.editorWidget = EditorWidget::InstanceReference;
    return property;
}

void updateMotor6D(Motor6D* self) {
    if (auto* workspace = self->findFirstAncestorWorkspace())
        if (auto* physics = static_cast<Workspace*>(workspace)->getPhysicsEngine())
            physics->updateConstraint(self->shared_from_this());
}

const bool s_motor6DRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Motor6D", "PhysicsConstraint", {
        partProperty("Part0", true),
        partProperty("Part1", false),
        custom("C0", PropType::CFrame, [](Instance* o) { return PropValue(static_cast<Motor6D*>(o)->C0); }, [](Instance* o, const PropValue& v) { static_cast<Motor6D*>(o)->setC0(std::get<CFrame>(v)); }),
        custom("C1", PropType::CFrame, [](Instance* o) { return PropValue(static_cast<Motor6D*>(o)->C1); }, [](Instance* o, const PropValue& v) { static_cast<Motor6D*>(o)->setC1(std::get<CFrame>(v)); }),
        custom("Transform", PropType::CFrame, [](Instance* o) { return PropValue(static_cast<Motor6D*>(o)->Transform); }, [](Instance* o, const PropValue& v) { static_cast<Motor6D*>(o)->setTransform(std::get<CFrame>(v)); }),
        custom("Frequency", PropType::Float, [](Instance* o) { return PropValue(static_cast<Motor6D*>(o)->Frequency); }, [](Instance* o, const PropValue& v) { static_cast<Motor6D*>(o)->setFrequency(std::get<float>(v)); }),
        custom("DampingRatio", PropType::Float, [](Instance* o) { return PropValue(static_cast<Motor6D*>(o)->DampingRatio); }, [](Instance* o, const PropValue& v) { static_cast<Motor6D*>(o)->setDampingRatio(std::get<float>(v)); }),
    });
    return true;
}();
}

Motor6D::Motor6D() : PhysicsConstraint("Motor6D") {}
std::shared_ptr<BaseCube> Motor6D::getPart0() const { return getCube0(); }
std::shared_ptr<BaseCube> Motor6D::getPart1() const { return getCube1(); }
void Motor6D::setPart0(std::shared_ptr<BaseCube> part) { setCube0(std::move(part)); }
void Motor6D::setPart1(std::shared_ptr<BaseCube> part) { setCube1(std::move(part)); }
void Motor6D::setC0(CFrame value) { C0 = value; updateMotor6D(this); }
void Motor6D::setC1(CFrame value) { C1 = value; updateMotor6D(this); }
void Motor6D::setTransform(CFrame value) { Transform = value; updateMotor6D(this); }
void Motor6D::setFrequency(float value) { if (std::isfinite(value) && value >= 0.0f) { Frequency = value; updateMotor6D(this); } }
void Motor6D::setDampingRatio(float value) { if (std::isfinite(value) && value >= 0.0f) { DampingRatio = value; updateMotor6D(this); } }
std::string Motor6D::getClassName() { return "Motor6D"; }
bool Motor6D::IsA(std::string className) { return className == "Motor6D" || PhysicsConstraint::IsA(className); }

void Motor6D::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Part0") { m_cube0Name = value.as<std::string>(); m_cube0.reset(); invalidateBinding(); resolveReferencesAndRegister(); return; }
    if (name == "Part1") { m_cube1Name = value.as<std::string>(); m_cube1.reset(); invalidateBinding(); resolveReferencesAndRegister(); return; }
    if (PropertyRegistry::loadProperty(this, "Motor6D", name, value)) return;
    PhysicsConstraint::setProperty(name, value);
}

std::shared_ptr<Instance> Motor6D::clone() const {
    auto copy = std::make_shared<Motor6D>();
    copy->Name = Name; copy->Enabled = Enabled;
    copy->m_cube0Name = m_cube0Name; copy->m_cube1Name = m_cube1Name;
    copy->m_cube0 = m_cube0; copy->m_cube1 = m_cube1;
    PropertyRegistry::cloneFields(this, copy.get(), "Motor6D");
    for (auto const& [name, child] : children) copy->addChild(child->clone());
    return copy;
}

void Motor6D::remapClonedInstances(const CloneRemap& map) {
    if (auto part = m_cube0.lock()) if (auto it = map.find(part.get()); it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second);
    if (auto part = m_cube1.lock()) if (auto it = map.find(part.get()); it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second);
}

void Motor6D::collectInstanceReferences(std::vector<InstanceReference>& out) {
    auto self = this;
    out.push_back({m_cube0.lock(), "BaseCube", "Motor6D.Part0", [self](std::shared_ptr<Instance> value) { self->setPart0(std::dynamic_pointer_cast<BaseCube>(value)); }});
    out.push_back({m_cube1.lock(), "BaseCube", "Motor6D.Part1", [self](std::shared_ptr<Instance> value) { self->setPart1(std::dynamic_pointer_cast<BaseCube>(value)); }});
}
