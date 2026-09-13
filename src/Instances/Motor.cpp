#include <include/Instances/Motor.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <cmath>
#include <utility>

static const bool s_motorRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Motor", "PhysicsConstraint", {
        instanceRefProperty<&PhysicsConstraint::m_cube0Name>("Cube0", "BaseCube"),
        instanceRefProperty<&PhysicsConstraint::m_cube1Name>("Cube1", "BaseCube"),
        instanceRefProperty<&Motor::m_attachment0Name>("Attachment0", "Attachment").omitEmpty(),
        instanceRefProperty<&Motor::m_attachment1Name>("Attachment1", "Attachment").omitEmpty(),
        custom("Axis", PropType::Vec3,
            [](Instance* instance) {
                return PropValue(static_cast<Motor*>(instance)->Axis);
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Motor*>(instance)->setAxis(std::get<Vector3>(value));
            }),
        method_prop<&Motor::getDriveVelocity, &Motor::setDriveVelocity>("DriveVelocity", -1.0e4f, 1.0e4f, 0.1f),
        method_prop<&Motor::getMaxForce, &Motor::setMaxForce>("MaxForce", 0.0f, 1.0e7f, 10.0f),
    });
    return true;
}();

Motor::Motor()
    : PhysicsConstraint("Motor") {}

Motor::Motor(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : PhysicsConstraint("Motor") {
    setCubes(std::move(cube0), std::move(cube1));
}

void Motor::refreshRefNames() {
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

void Motor::resolveAdditionalReferences() {
    if (!m_attachment0.lock() && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0 = Attachment::findUnder(c0.get(), m_attachment0Name);
    if (!m_attachment1.lock() && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1 = Attachment::findUnder(c1.get(), m_attachment1Name);
}

void Motor::recreateConstraint() {
    if (!m_constraintHandle || !m_lastWorkspace ||
        !m_lastWorkspace->getPhysicsEngine()) return;
    auto self = std::static_pointer_cast<Motor>(shared_from_this());
    Physics* physics = m_lastWorkspace->getPhysicsEngine();
    physics->removeConstraint(self);
    physics->createMotor(self);
    if (!m_constraintHandle) m_lastWorkspace->registerConstraint(self);
}

void Motor::setDriveVelocity(float v) {
    if (!std::isfinite(v)) return;
    if (DriveVelocity == v && (v == 0.0f || MaxForce <= 0.0f)) return;
    DriveVelocity = v;
    if (m_constraintHandle && m_lastWorkspace && m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->updateConstraint(shared_from_this());
}

void Motor::setMaxForce(float v) {
    if (!std::isfinite(v) || v < 0.0f) return;
    if (MaxForce == v) return;
    MaxForce = v;
    if (m_constraintHandle && m_lastWorkspace && m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->updateConstraint(shared_from_this());
}

void Motor::setAxis(Vector3 axis) {
    if (!std::isfinite(axis.x) || !std::isfinite(axis.y) ||
        !std::isfinite(axis.z) || axis.length() <= 1.0e-6f)
        return;
    if (Axis == axis) return;
    Axis = axis;
}
PhysicsConstraintHandle Motor::getConstraintHandle() const {
    return m_constraintHandle;
}

std::shared_ptr<Instance> Motor::clone() const {
    auto c = std::make_shared<Motor>();
    c->Name          = Name;
    PropertyRegistry::cloneFields(this, c.get(), "Motor");
    c->m_cube0       = m_cube0;
    c->m_cube1       = m_cube1;
    c->m_attachment0 = m_attachment0;
    c->m_attachment1 = m_attachment1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Motor::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto a0 = m_attachment0.lock()) { auto it = map.find(a0.get()); if (it != map.end()) m_attachment0 = std::static_pointer_cast<Attachment>(it->second); }
    if (auto a1 = m_attachment1.lock()) { auto it = map.find(a1.get()); if (it != map.end()) m_attachment1 = std::static_pointer_cast<Attachment>(it->second); }
}

std::string Motor::getClassName() { return "Motor"; }

bool Motor::IsA(std::string className) {
    if (className == "Motor") return true;
    return PhysicsConstraint::IsA(className);
}

void Motor::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else if (name == "Axis") {
        setAxis(Vector3(value[0].as<float>(), value[1].as<float>(), value[2].as<float>()));
    } else if (name == "DriveVelocity") {
        setDriveVelocity(value.as<float>());
    } else if (name == "MaxForce") {
        setMaxForce(value.as<float>());
    } else {
        PhysicsConstraint::setProperty(name, value);
    }
        resolveAdditionalReferences();
    registerIfReady();
}
