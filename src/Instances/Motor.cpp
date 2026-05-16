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

void Motor::setDriveVelocity(float v) {
    DriveVelocity = v;
    if (m_joint) m_joint->setDriveVelocity(v);
}

void Motor::setMaxForce(float v) {
    MaxForce = v;
    if (m_joint) m_joint->setDriveForceLimit(v);
}

std::string Motor::GetClassName() { return "Motor"; }

bool Motor::IsA(std::string className) {
    if (className == "Motor") return true;
    return Instance::IsA(className);
}

void Motor::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Cube0") {
        m_cube0Name = value.as<std::string>();
        if (auto* ws_raw = findFirstAncestorWorkspace()) {
            auto* child = ws_raw->getChildByPath(m_cube0Name);
            if (child && child->IsA("BaseCube"))
                m_cube0 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
        }
    } else if (name == "Cube1") {
        m_cube1Name = value.as<std::string>();
        if (auto* ws_raw = findFirstAncestorWorkspace()) {
            auto* child = ws_raw->getChildByPath(m_cube1Name);
            if (child && child->IsA("BaseCube"))
                m_cube1 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
        }
    } else if (name == "Axis") {
        Axis.x = value[0].as<float>();
        Axis.y = value[1].as<float>();
        Axis.z = value[2].as<float>();
    } else if (name == "DriveVelocity") {
        setDriveVelocity(value.as<float>());
    } else if (name == "MaxForce") {
        setMaxForce(value.as<float>());
    } else {
        Instance::setProperty(name, value);
    }
    if (m_cube0.lock() && m_cube1.lock()) {
        if (auto* ws_raw = findFirstAncestorWorkspace())
            static_cast<Workspace*>(ws_raw)->registerConstraint(shared_from_this());
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
