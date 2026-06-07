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

std::string Rod::getClassName() { return "Rod"; }

bool Rod::IsA(std::string className) {
    if (className == "Rod") return true;
    return Instance::IsA(className);
}

void Rod::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else if (name == "Color") {
        Color.r = value[0].as<float>();
        Color.g = value[1].as<float>();
        Color.b = value[2].as<float>();
        Color.a = value[3].as<float>();
    } else if (name == "LineWidth") {
        LineWidth = value.as<float>();
    } else {
        Instance::setProperty(name, value);
    }
    if (m_cube0.lock() && m_cube1.lock()) {
        if (auto* ws_raw = findFirstAncestorWorkspace())
            static_cast<Workspace*>(ws_raw)->registerConstraint(shared_from_this());
    }
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
