#include <Instances/Workspace.hpp>
#include <Core/Physics.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>
#include <algorithm>

static const bool s_workspaceRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Workspace", {
        field<&Workspace::Gravity>("Gravity"),
        field<&Workspace::Wind>("Wind"),
        field<&Workspace::PhysicsEnabled>("PhysicsEnabled"),
    });
    return true;
}();

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

void Workspace::unregisterCube(const Instance* c) {
    pendingInstances.erase(
        std::remove_if(pendingInstances.begin(), pendingInstances.end(),
            [c](const std::shared_ptr<Instance>& value) {
                return !value || value.get() == c;
            }),
        pendingInstances.end());
}

void Workspace::registerConstraint(const std::shared_ptr<Instance>& c) {
    if (std::find(pendingConstraints.begin(), pendingConstraints.end(), c) == pendingConstraints.end()) {
        pendingConstraints.push_back(c);
    }
}

void Workspace::unregisterConstraint(const Instance* c) {
    pendingConstraints.erase(
        std::remove_if(pendingConstraints.begin(), pendingConstraints.end(),
            [c](const std::shared_ptr<Instance>& value) {
                return !value || value.get() == c;
            }),
        pendingConstraints.end());
}

Workspace::Workspace() : Instance("Workspace") {}

Workspace::~Workspace() {
    for (auto& [name, child] : children) {
        child->Parent = {};
        child->onAncestorChanged();
    }
    this->children.clear();
}

std::shared_ptr<Instance> Workspace::clone() const {
    auto copy = std::make_shared<Workspace>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Workspace");
    for (const auto& [name, child] : children) {
        if (child) copy->addChild(child->clone());
    }
    return copy;
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
    if (PropertyRegistry::loadProperty(this, "Workspace", name, value)) return;
    Instance::setProperty(name, value);
}

void Workspace::initPhysics() {
    if (m_ownedPhysics) return; // 既に初期化済み
    m_ownedPhysics = std::make_unique<Physics>();
    m_ownedPhysics->init();
    physicsEngine = m_ownedPhysics.get();
    if (!m_ownedPhysics->isAvailable())
        RCBN_ERROR("Workspace physics backend is unavailable");
}
