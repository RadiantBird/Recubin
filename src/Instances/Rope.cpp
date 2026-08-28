#include <include/Instances/Rope.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Core/Physics.hpp>
#include <cmath>
#include <utility>

Rope::Rope()
    : PhysicsConstraint("Rope") {}

Rope::Rope(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : PhysicsConstraint("Rope") {
    setCubes(std::move(cube0), std::move(cube1));
}

void Rope::refreshRefNames() {
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

void Rope::registerIfReady() {
    if (!Enabled) return;
    auto* ws_raw = findFirstAncestorWorkspace();
    if (!ws_raw) return;
    Workspace* ws = static_cast<Workspace*>(ws_raw);
    // 片方だけ名前で指定され未解決のCubeを、保存済みの名前から遅延解決する（Weld と同じ理由）
    if (!m_cube0.lock() && !m_cube0Name.empty()) {
        auto* child = ws->getChildByPath(m_cube0Name);
        if (child && child->IsA("BaseCube"))
            m_cube0 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
    }
    if (!m_cube1.lock() && !m_cube1Name.empty()) {
        auto* child = ws->getChildByPath(m_cube1Name);
        if (child && child->IsA("BaseCube"))
            m_cube1 = std::static_pointer_cast<BaseCube>(child->shared_from_this());
    }
    resolveAttachments();
    if (m_cube0.lock() && m_cube1.lock())
        ws->registerConstraint(shared_from_this());
}

void Rope::resolveAttachments() {
    if (!m_attachment0.lock() && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0 = Attachment::findUnder(c0.get(), m_attachment0Name);
    if (!m_attachment1.lock() && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1 = Attachment::findUnder(c1.get(), m_attachment1Name);
}

void Rope::setMaxDistance(float v) {
    if (!std::isfinite(v) || v < 0.0f || MaxDistance == v) return;
    MaxDistance = v;
    if (m_constraintHandle && m_lastWorkspace && m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->updateConstraint(shared_from_this());
}

void Rope::setStiffness(float v) {
    if (!std::isfinite(v) || v < 0.0f || Stiffness == v) return;
    Stiffness = v;
    if (m_constraintHandle && m_lastWorkspace && m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->updateConstraint(shared_from_this());
}

void Rope::setDamping(float v) {
    if (!std::isfinite(v) || v < 0.0f || Damping == v) return;
    Damping = v;
    if (m_constraintHandle && m_lastWorkspace && m_lastWorkspace->getPhysicsEngine())
        m_lastWorkspace->getPhysicsEngine()->updateConstraint(shared_from_this());
}
std::shared_ptr<Instance> Rope::clone() const {
    auto c = std::make_shared<Rope>();
    c->Name        = Name;
    c->Enabled     = Enabled;
    c->m_cube0Name = m_cube0Name;
    c->m_cube1Name = m_cube1Name;
    c->m_attachment0Name = m_attachment0Name;
    c->m_attachment1Name = m_attachment1Name;
    c->MaxDistance = MaxDistance;
    c->Stiffness   = Stiffness;
    c->Damping     = Damping;
    c->Color       = Color;
    c->LineWidth   = LineWidth;
    c->m_cube0     = m_cube0;
    c->m_cube1     = m_cube1;
    c->m_attachment0 = m_attachment0;
    c->m_attachment1 = m_attachment1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Rope::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto a0 = m_attachment0.lock()) { auto it = map.find(a0.get()); if (it != map.end()) m_attachment0 = std::static_pointer_cast<Attachment>(it->second); }
    if (auto a1 = m_attachment1.lock()) { auto it = map.find(a1.get()); if (it != map.end()) m_attachment1 = std::static_pointer_cast<Attachment>(it->second); }
}

std::string Rope::getClassName() { return "Rope"; }

bool Rope::IsA(std::string className) {
    if (className == "Rope") return true;
    return PhysicsConstraint::IsA(className);
}

void Rope::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else PhysicsConstraint::setProperty(name, value);
    registerIfReady();
}
