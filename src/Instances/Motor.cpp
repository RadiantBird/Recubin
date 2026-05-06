#include <include/Instances/Motor.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>

Motor::Motor()
    : Instance("Motor") {}

Motor::Motor(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : Instance("Motor"), m_cube0(cube0), m_cube1(cube1) {}

Motor::~Motor() {
    if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_joint) {
        m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
    }
    m_joint = nullptr;
}

void Motor::setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1) {
    m_cube0 = cube0;
    m_cube1 = cube1;
}

std::string Motor::GetClassName() { return "Motor"; }

bool Motor::IsA(std::string className) {
    if (className == "Motor") return true;
    return Instance::IsA(className);
}

void Motor::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Cube0") {
        m_cube0Name = value.as<std::string>();
    } else if (name == "Cube1") {
        m_cube1Name = value.as<std::string>();
    } else if (name == "Axis") {
        Axis.x = value[0].as<float>();
        Axis.y = value[1].as<float>();
        Axis.z = value[2].as<float>();
    } else if (name == "DriveVelocity") {
        DriveVelocity = value.as<float>();
    } else if (name == "MaxForce") {
        MaxForce = value.as<float>();
    } else {
        Instance::setProperty(name, value);
    }
}

void Motor::onAncestorChanged() {
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
