#include <Instances/Workspace.hpp>
#include <Core/Physics.hpp>
#include <algorithm>

void Workspace::registerScript(const std::shared_ptr<Instance>& s) {
    scripts.push_back(s);
}

void Workspace::unregisterScript(const std::shared_ptr<Instance>& s) {
    scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
}

void Workspace::registerCube(const std::shared_ptr<Instance>& c) {
    if (std::find(pendingInstances.begin(), pendingInstances.end(), c) == pendingInstances.end()) {
        pendingInstances.push_back(c);
    }
}

void Workspace::registerConstraint(const std::shared_ptr<Instance>& c) {
    if (std::find(pendingConstraints.begin(), pendingConstraints.end(), c) == pendingConstraints.end()) {
        pendingConstraints.push_back(c);
    }
}

Workspace::Workspace() : Instance("Workspace") {}

Workspace::~Workspace() {
    for (auto& [name, child] : children) {
        child->Parent = {};
        child->onAncestorChanged();
    }
    this->children.clear();
}

std::string Workspace::getClassName() {
    return "Workspace";
}

bool Workspace::IsA(std::string className) {
    if (className == "Workspace") {
        return true;
    }
    return Instance::IsA(className);
}

void Workspace::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Gravity") {
        if (value.IsSequence() && value.size() == 3) {
            Gravity.x = value[0].as<float>();
            Gravity.y = value[1].as<float>();
            Gravity.z = value[2].as<float>();
        }
    } else if (name == "PhysicsEnabled") {
        PhysicsEnabled = value.as<bool>();
    } else {
        Instance::setProperty(name, value);
    }
}

void Workspace::initPhysics() {
    if (m_ownedPhysics) return; // 既に初期化済み
    m_ownedPhysics = std::make_unique<Physics>();
    m_ownedPhysics->init();
    physicsEngine = m_ownedPhysics.get();
}

