#include <include/Instances/Spring.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <cmath>
#include <utility>

static const bool s_springRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Spring", "PhysicsConstraint", {
        instanceRefProperty<&PhysicsConstraint::m_cube0Name>("Cube0", "BaseCube"),
        instanceRefProperty<&PhysicsConstraint::m_cube1Name>("Cube1", "BaseCube"),
        instanceRefProperty<&Spring::m_attachment0Name>("Attachment0", "Attachment").omitEmpty(),
        instanceRefProperty<&Spring::m_attachment1Name>("Attachment1", "Attachment").omitEmpty(),
        method_prop<&Spring::getFreeLength, &Spring::setFreeLength>("FreeLength", 0.0f, 1.0e6f, 0.1f),
        method_prop<&Spring::getStiffness, &Spring::setStiffness>("Stiffness", 0.0f, 1.0e6f, 1.0f),
        method_prop<&Spring::getDamping, &Spring::setDamping>("Damping", 0.0f, 1.0e6f, 0.1f),
        field<&Spring::Visible>("Visible"),
        field<&Spring::Color>("Color"),
        field<&Spring::Radius>("Radius", 0.01f, 1000.0f, 0.05f),
        field<&Spring::Coils>("Coils", 1.0f, 200.0f, 1.0f),
        field<&Spring::Thickness>("Thickness", 0.005f, 100.0f, 0.01f),
    });
    return true;
}();

Spring::Spring()
    : PhysicsConstraint("Spring") {}

Spring::Spring(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : PhysicsConstraint("Spring") {
    setCubes(std::move(cube0), std::move(cube1));
}

void Spring::refreshRefNames() {
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

void Spring::resolveAdditionalReferences() {
    if (!m_attachment0.lock() && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0 = Attachment::resolveReference(c0.get(), m_attachment0Name);
    if (!m_attachment1.lock() && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1 = Attachment::resolveReference(c1.get(), m_attachment1Name);
}

void Spring::notifyPhysicsUpdate() {
    if (m_constraintHandle && m_lastWorkspace && m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->updateConstraint(shared_from_this());
}

void Spring::setFreeLength(float v) {
    if (!std::isfinite(v) || v < 0.0f || FreeLength == v) return;
    FreeLength = v;
    notifyPhysicsUpdate();
}

void Spring::setStiffness(float v) {
    if (!std::isfinite(v) || v < 0.0f || Stiffness == v) return;
    Stiffness = v;
    notifyPhysicsUpdate();
}

void Spring::setDamping(float v) {
    if (!std::isfinite(v) || v < 0.0f || Damping == v) return;
    Damping = v;
    notifyPhysicsUpdate();
}

std::shared_ptr<Instance> Spring::clone() const {
    auto c = std::make_shared<Spring>();
    c->Name        = Name;
    PropertyRegistry::cloneFields(this, c.get(), "Spring");
    c->m_cube0     = m_cube0;
    c->m_cube1     = m_cube1;
    c->m_attachment0 = m_attachment0;
    c->m_attachment1 = m_attachment1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Spring::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto a0 = m_attachment0.lock()) { auto it = map.find(a0.get()); if (it != map.end()) m_attachment0 = std::static_pointer_cast<Attachment>(it->second); }
    if (auto a1 = m_attachment1.lock()) { auto it = map.find(a1.get()); if (it != map.end()) m_attachment1 = std::static_pointer_cast<Attachment>(it->second); }
    refreshRefNames();
}

std::string Spring::getClassName() { return "Spring"; }

bool Spring::IsA(std::string className) {
    if (className == "Spring") return true;
    return PhysicsConstraint::IsA(className);
}

void Spring::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else if (name == "FreeLength")   setFreeLength(value.as<float>());
    else if (name == "Stiffness")      setStiffness(value.as<float>());
    else if (name == "Damping")        setDamping(value.as<float>());
    else if (name == "Visible")        Visible = value.as<bool>();
    else if (name == "Color") {
        Color.r = value[0].as<float>();
        Color.g = value[1].as<float>();
        Color.b = value[2].as<float>();
        Color.a = value[3].as<float>();
    } else if (name == "Radius")       Radius = value.as<float>();
    else if (name == "Coils")          Coils = value.as<int>();
    else if (name == "Thickness")      Thickness = value.as<float>();
    else PhysicsConstraint::setProperty(name, value);
    registerIfReady();
}
