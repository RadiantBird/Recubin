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

void Rope::setMaxDistance(float v) {
    MaxDistance = v;
    if (m_joint) m_joint->setMaxDistance(v);
}

void Rope::setStiffness(float v) {
    Stiffness = v;
    if (m_joint) {
        m_joint->setStiffness(v);
        m_joint->setDistanceJointFlag(physx::PxDistanceJointFlag::eSPRING_ENABLED, v > 0.0f);
    }
}

void Rope::setDamping(float v) {
    Damping = v;
    if (m_joint) m_joint->setDamping(v);
}

std::string Rope::GetClassName() { return "Rope"; }

bool Rope::IsA(std::string className) {
    if (className == "Rope") return true;
    return Instance::IsA(className);
}

void Rope::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else if (name == "MaxDistance")  setMaxDistance(value.as<float>());
    else if (name == "Stiffness")      setStiffness(value.as<float>());
    else if (name == "Damping")        setDamping(value.as<float>());
    else if (name == "Color") {
        Color.r = value[0].as<float>();
        Color.g = value[1].as<float>();
        Color.b = value[2].as<float>();
        Color.a = value[3].as<float>();
    } else if (name == "LineWidth") {
        LineWidth = value.as<float>();
    } else Instance::setProperty(name, value);
    if (m_cube0.lock() && m_cube1.lock()) {
        if (auto* ws_raw = findFirstAncestorWorkspace())
            static_cast<Workspace*>(ws_raw)->registerConstraint(shared_from_this());
    }
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
