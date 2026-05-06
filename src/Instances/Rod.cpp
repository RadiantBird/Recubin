#include <include/Instances/Rod.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>

Rod::Rod()
    : Instance("Rod") {}

Rod::Rod(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : Instance("Rod"), m_cube0(cube0), m_cube1(cube1) {}

Rod::~Rod() {
    if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_joint) {
        m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
    }
    m_joint = nullptr;
}

void Rod::setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1) {
    m_cube0 = cube0;
    m_cube1 = cube1;
}

std::string Rod::GetClassName() { return "Rod"; }

bool Rod::IsA(std::string className) {
    if (className == "Rod") return true;
    return Instance::IsA(className);
}

void Rod::setProperty(const std::string& name, const YAML::Node& value) {
    if      (name == "Cube0") m_cube0Name = value.as<std::string>();
    else if (name == "Cube1") m_cube1Name = value.as<std::string>();
    else Instance::setProperty(name, value);
}

void Rod::onAncestorChanged() {
    Instance* ws_raw = findFirstAncestorWorkspace();
    if (ws_raw) {
        Workspace* ws = static_cast<Workspace*>(ws_raw);
        ws->registerConstraint(shared_from_this());
        m_lastWorkspace = ws;
    } else {
        if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_joint) {
            m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
        }
        m_lastWorkspace = nullptr;
    }
    Instance::onAncestorChanged();
}
