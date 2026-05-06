#include <include/Instances/Rope.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>

Rope::Rope()
    : Instance("Rope") {}

Rope::Rope(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : Instance("Rope"), m_cube0(cube0), m_cube1(cube1) {}

Rope::~Rope() {
    if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_joint) {
        m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
    }
    m_joint = nullptr;
}

void Rope::setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1) {
    m_cube0 = cube0;
    m_cube1 = cube1;
}

std::string Rope::GetClassName() { return "Rope"; }

bool Rope::IsA(std::string className) {
    if (className == "Rope") return true;
    return Instance::IsA(className);
}

void Rope::setProperty(const std::string& name, const YAML::Node& value) {
    if      (name == "Cube0")        m_cube0Name   = value.as<std::string>();
    else if (name == "Cube1")        m_cube1Name   = value.as<std::string>();
    else if (name == "MaxDistance")  MaxDistance   = value.as<float>();
    else if (name == "Stiffness")    Stiffness     = value.as<float>();
    else if (name == "Damping")      Damping       = value.as<float>();
    else Instance::setProperty(name, value);
}

void Rope::onAncestorChanged() {
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
