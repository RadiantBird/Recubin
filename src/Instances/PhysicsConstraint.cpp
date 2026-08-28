#include <include/Instances/PhysicsConstraint.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>

PhysicsConstraint::PhysicsConstraint(const std::string& className)
    : Instance(className) {}

PhysicsConstraint::~PhysicsConstraint() {
    if (m_lastWorkspace) m_lastWorkspace->unregisterConstraint(this);
    m_constraintHandle = {};
}

bool PhysicsConstraint::IsA(std::string className) {
    if (className == "PhysicsConstraint") return true;
    return Instance::IsA(className);
}

void PhysicsConstraint::setCubes(std::shared_ptr<BaseCube> cube0,
                                 std::shared_ptr<BaseCube> cube1) {
    m_cube0 = cube0;
    m_cube1 = cube1;
    m_cube0Name = cube0 ? cube0->getWorkspaceRelativePath() : "";
    m_cube1Name = cube1 ? cube1->getWorkspaceRelativePath() : "";
    registerIfReady();
}

void PhysicsConstraint::setCube0(std::shared_ptr<BaseCube> cube) {
    m_cube0 = cube;
    m_cube0Name = cube ? cube->getWorkspaceRelativePath() : "";
    registerIfReady();
}

void PhysicsConstraint::setCube1(std::shared_ptr<BaseCube> cube) {
    m_cube1 = cube;
    m_cube1Name = cube ? cube->getWorkspaceRelativePath() : "";
    registerIfReady();
}

std::shared_ptr<BaseCube> PhysicsConstraint::getCube0() const { return m_cube0.lock(); }
std::shared_ptr<BaseCube> PhysicsConstraint::getCube1() const { return m_cube1.lock(); }

void PhysicsConstraint::refreshRefNames() {
    if (auto cube0 = m_cube0.lock(); cube0 && !m_cube0Name.empty())
        m_cube0Name = cube0->getWorkspaceRelativePath();
    if (auto cube1 = m_cube1.lock(); cube1 && !m_cube1Name.empty())
        m_cube1Name = cube1->getWorkspaceRelativePath();
}

void PhysicsConstraint::setEnabled(bool enabled) {
    if (Enabled == enabled) return;
    Enabled = enabled;
    if (!Enabled && m_constraintHandle && m_lastWorkspace &&
        m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
    if (Enabled) registerIfReady();
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
        if (workspace && Enabled) workspace->registerConstraint(shared_from_this());
    }
    Instance::onAncestorChanged();
}

void PhysicsConstraint::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Enabled") setEnabled(value.as<bool>());
    else Instance::setProperty(name, value);
}
