#include <include/Instances/PhysicsConstraint.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_physicsConstraintRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("PhysicsConstraint", "Instance", {
        custom("Enabled", PropType::Bool,
            [](Instance* instance) {
                return PropValue(static_cast<PhysicsConstraint*>(instance)->Enabled);
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<PhysicsConstraint*>(instance)->setEnabled(std::get<bool>(value));
            }),
    });
    return true;
}();

PhysicsConstraint::PhysicsConstraint(const std::string& className)
    : Instance(className) {}

PhysicsConstraint::~PhysicsConstraint() {
    if (m_lastWorkspace) m_lastWorkspace->unregisterConstraint(this);
    m_constraintHandle = {};
}

bool PhysicsConstraint::endpointsReady() const {
    return !m_cube0.expired() && !m_cube1.expired();
}

void PhysicsConstraint::registerIfReady() {
    if (!Enabled) return;
    auto* workspaceRaw = findFirstAncestorWorkspace();
    if (!workspaceRaw) return;
    auto* workspace = static_cast<Workspace*>(workspaceRaw);
    auto resolveCube = [&](std::weak_ptr<BaseCube>& target, const std::string& name) {
        if (target.lock() || name.empty()) return;
        Instance* found = workspace->getChildByPath(name);
        if (found && found->IsA("BaseCube"))
            target = std::static_pointer_cast<BaseCube>(found->shared_from_this());
    };
    resolveCube(m_cube0, m_cube0Name);
    resolveCube(m_cube1, m_cube1Name);
    resolveAdditionalReferences();
    if (!endpointsReady() || !additionalReferencesReady()) return;
    if (!m_constraintHandle) workspace->registerConstraint(shared_from_this());
}

void PhysicsConstraint::resolveReferencesAndRegister() {
    registerIfReady();
}

bool PhysicsConstraint::IsA(std::string className) {
    if (className == "PhysicsConstraint") return true;
    return Instance::IsA(className);
}

void PhysicsConstraint::setCubes(std::shared_ptr<BaseCube> cube0,
                                 std::shared_ptr<BaseCube> cube1) {
    invalidateBinding();
    m_cube0 = cube0;
    m_cube1 = cube1;
    m_cube0Name = cube0 ? cube0->getWorkspaceRelativePath() : "";
    m_cube1Name = cube1 ? cube1->getWorkspaceRelativePath() : "";
    registerIfReady();
}

void PhysicsConstraint::setCube0(std::shared_ptr<BaseCube> cube) {
    invalidateBinding();
    m_cube0 = cube;
    m_cube0Name = cube ? cube->getWorkspaceRelativePath() : "";
    registerIfReady();
}

void PhysicsConstraint::setCube1(std::shared_ptr<BaseCube> cube) {
    invalidateBinding();
    m_cube1 = cube;
    m_cube1Name = cube ? cube->getWorkspaceRelativePath() : "";
    registerIfReady();
}

std::shared_ptr<BaseCube> PhysicsConstraint::getCube0() const { return m_cube0.lock(); }
std::shared_ptr<BaseCube> PhysicsConstraint::getCube1() const { return m_cube1.lock(); }
PhysicsConstraintHandle PhysicsConstraint::getConstraintHandle() const {
    return m_constraintHandle;
}

void PhysicsConstraint::refreshRefNames() {
    if (auto cube0 = m_cube0.lock(); cube0 && !m_cube0Name.empty())
        m_cube0Name = cube0->getWorkspaceRelativePath();
    if (auto cube1 = m_cube1.lock(); cube1 && !m_cube1Name.empty())
        m_cube1Name = cube1->getWorkspaceRelativePath();
}

void PhysicsConstraint::setEnabled(bool enabled) {
    if (Enabled == enabled) return;
    invalidateBinding();
    Enabled = enabled;
    if (Enabled) registerIfReady();
}

void PhysicsConstraint::invalidateBinding() {
    if (!m_constraintHandle || !m_lastWorkspace ||
        !m_lastWorkspace->getPhysicsEngine()) return;
    m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
    m_constraintHandle = {};
}

void PhysicsConstraint::onAncestorChanged() {
    auto* workspace = static_cast<Workspace*>(findFirstAncestorWorkspace());
    if (workspace != m_lastWorkspace) {
        if (m_lastWorkspace) {
            m_lastWorkspace->unregisterConstraint(this);
            if (m_lastWorkspace->getPhysicsEngine() && m_constraintHandle)
                m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
        }
        m_lastWorkspace = workspace;
        registerIfReady();
    }
    Instance::onAncestorChanged();
}

void PhysicsConstraint::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "PhysicsConstraint", name, value)) return;
    Instance::setProperty(name, value);
}
