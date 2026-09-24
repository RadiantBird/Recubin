#include <include/Instances/NoCollision.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_noCollisionRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("NoCollision", "PhysicsConstraint", {
        instanceRefProperty<&PhysicsConstraint::m_cube0Name>("Cube0", "BaseCube"),
        instanceRefProperty<&PhysicsConstraint::m_cube1Name>("Cube1", "BaseCube"),
    });
    return true;
}();
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <utility>

NoCollision::NoCollision()
    : PhysicsConstraint("NoCollision") {}

NoCollision::NoCollision(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : PhysicsConstraint("NoCollision") {
    setCubes(std::move(cube0), std::move(cube1));
}

void NoCollision::refreshRefNames() {
    PhysicsConstraint::refreshRefNames();
    if (auto c0 = m_cube0.lock(); c0 && !m_cube0Name.empty())
        m_cube0Name = c0->getWorkspaceRelativePath();
    if (auto c1 = m_cube1.lock(); c1 && !m_cube1Name.empty())
        m_cube1Name = c1->getWorkspaceRelativePath();
}

std::string NoCollision::getClassName() { return "NoCollision"; }

bool NoCollision::IsA(std::string className) {
    if (className == "NoCollision") return true;
    return PhysicsConstraint::IsA(className);
}

void NoCollision::setProperty(const std::string& name, const YAML::Node& value) {
    // Workspace 配下なら Workspace 相対、そうでなければ最上位祖先(System 等)相対で解決する。
    auto resolveCube = [this](const std::string& cubeName) -> std::shared_ptr<BaseCube> {
        Instance* found = nullptr;
        if (Instance* ws = findFirstAncestorWorkspace())
            found = ws->getChildByPath(cubeName);
        if (!(found && found->IsA("BaseCube"))) {
            Instance* top = this;
            for (auto p = Parent.lock(); p; p = p->Parent.lock()) top = p.get();
            found = top->getChildByPath(cubeName);
        }
        if (found && found->IsA("BaseCube"))
            return std::static_pointer_cast<BaseCube>(found->shared_from_this());
        return nullptr;
    };

    if (name == "Cube0") {
        m_cube0Name = value.as<std::string>();
        m_cube0.reset();
        if (auto c = resolveCube(m_cube0Name)) m_cube0 = c;
    } else if (name == "Cube1") {
        m_cube1Name = value.as<std::string>();
        m_cube1.reset();
        if (auto c = resolveCube(m_cube1Name)) m_cube1 = c;
    } else {
        PhysicsConstraint::setProperty(name, value);
    }
    registerIfReady();
}

std::shared_ptr<Instance> NoCollision::clone() const {
    auto c = std::make_shared<NoCollision>();
    c->Name        = Name;
    PropertyRegistry::cloneFields(this, c.get(), "NoCollision");
    c->m_cube0     = m_cube0;   // 一旦は元キューブを指す（rebindClonedConstraints が張り替える）
    c->m_cube1     = m_cube1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void NoCollision::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    refreshRefNames();
}
