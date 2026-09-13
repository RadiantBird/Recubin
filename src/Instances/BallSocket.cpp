#include <include/Instances/BallSocket.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <utility>

static const bool s_ballSocketRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("BallSocket", "PhysicsConstraint", {
        instanceRefProperty<&PhysicsConstraint::m_cube0Name>("Cube0", "BaseCube"),
        instanceRefProperty<&PhysicsConstraint::m_cube1Name>("Cube1", "BaseCube"),
        instanceRefProperty<&BallSocket::m_attachment0Name>("Attachment0", "Attachment").omitEmpty(),
        instanceRefProperty<&BallSocket::m_attachment1Name>("Attachment1", "Attachment").omitEmpty(),
    });
    return true;
}();

BallSocket::BallSocket()
    : PhysicsConstraint("BallSocket") {}

BallSocket::BallSocket(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : PhysicsConstraint("BallSocket") {
    setCubes(std::move(cube0), std::move(cube1));
}

void BallSocket::refreshRefNames() {
    PhysicsConstraint::refreshRefNames();
    if (auto c0 = m_cube0.lock(); c0 && !m_cube0Name.empty())
        m_cube0Name = c0->getWorkspaceRelativePath();
    if (auto c1 = m_cube1.lock(); c1 && !m_cube1Name.empty())
        m_cube1Name = c1->getWorkspaceRelativePath();
    if (auto a0 = m_attachment0.lock(); a0 && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0Name = a0->getPathUpTo(c0.get());
    if (auto a1 = m_attachment1.lock(); a1 && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1Name = a1->getPathUpTo(c1.get());
}

void BallSocket::resolveAdditionalReferences() {
    if (!m_attachment0.lock() && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0 = Attachment::findUnder(c0.get(), m_attachment0Name);
    if (!m_attachment1.lock() && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1 = Attachment::findUnder(c1.get(), m_attachment1Name);
}
std::shared_ptr<Instance> BallSocket::clone() const {
    auto c = std::make_shared<BallSocket>();
    c->Name        = Name;
    PropertyRegistry::cloneFields(this, c.get(), "BallSocket");
    c->m_cube0     = m_cube0;
    c->m_cube1     = m_cube1;
    c->m_attachment0 = m_attachment0;
    c->m_attachment1 = m_attachment1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void BallSocket::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto a0 = m_attachment0.lock()) { auto it = map.find(a0.get()); if (it != map.end()) m_attachment0 = std::static_pointer_cast<Attachment>(it->second); }
    if (auto a1 = m_attachment1.lock()) { auto it = map.find(a1.get()); if (it != map.end()) m_attachment1 = std::static_pointer_cast<Attachment>(it->second); }
}

std::string BallSocket::getClassName() { return "BallSocket"; }

bool BallSocket::IsA(std::string className) {
    if (className == "BallSocket") return true;
    return PhysicsConstraint::IsA(className);
}

void BallSocket::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Cube0") {
        m_cube0Name = value.as<std::string>();
        m_cube0.reset();
        if (auto* ws_raw = findFirstAncestorWorkspace()) {
            auto* child = ws_raw->getChildByPath(m_cube0Name);
            if (child && child->IsA("BaseCube"))
                m_cube0 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
        }
    } else if (name == "Cube1") {
        m_cube1Name = value.as<std::string>();
        m_cube1.reset();
        if (auto* ws_raw = findFirstAncestorWorkspace()) {
            auto* child = ws_raw->getChildByPath(m_cube1Name);
            if (child && child->IsA("BaseCube"))
                m_cube1 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
        }
    } else if (name == "Attachment0") {
        m_attachment0Name = value.as<std::string>();
        m_attachment0.reset(); // 名前変更後に registerIfReady() 経由で再解決させる
    } else if (name == "Attachment1") {
        m_attachment1Name = value.as<std::string>();
        m_attachment1.reset();
    } else {
        PhysicsConstraint::setProperty(name, value);
    }
    registerIfReady();
}
