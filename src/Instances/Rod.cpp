#include <include/Instances/Rod.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Attachment.hpp>
#include <include/Core/Physics.hpp>

Rod::Rod()
    : Instance("Rod") {}

Rod::Rod(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : Instance("Rod"), m_cube0(cube0), m_cube1(cube1),
      m_cube0Name(cube0 ? cube0->getWorkspaceRelativePath() : ""),
      m_cube1Name(cube1 ? cube1->getWorkspaceRelativePath() : "") {}

Rod::~Rod() {
    if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_constraintHandle) {
        m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
    }
    m_constraintHandle = {};
}

void Rod::setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1) {
    m_cube0 = cube0;
    m_cube1 = cube1;
    m_cube0Name = cube0 ? cube0->getWorkspaceRelativePath() : "";
    m_cube1Name = cube1 ? cube1->getWorkspaceRelativePath() : "";
}

void Rod::setCube0(std::shared_ptr<BaseCube> cube) {
    m_cube0 = cube;
    m_cube0Name = cube ? cube->getWorkspaceRelativePath() : "";
    registerIfReady();
}

void Rod::setCube1(std::shared_ptr<BaseCube> cube) {
    m_cube1 = cube;
    m_cube1Name = cube ? cube->getWorkspaceRelativePath() : "";
    registerIfReady();
}

void Rod::refreshRefNames() {
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

void Rod::registerIfReady() {
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

void Rod::resolveAttachments() {
    if (!m_attachment0.lock() && !m_attachment0Name.empty())
        if (auto c0 = m_cube0.lock())
            m_attachment0 = Attachment::findUnder(c0.get(), m_attachment0Name);
    if (!m_attachment1.lock() && !m_attachment1Name.empty())
        if (auto c1 = m_cube1.lock())
            m_attachment1 = Attachment::findUnder(c1.get(), m_attachment1Name);
}

std::shared_ptr<Instance> Rod::clone() const {
    auto c = std::make_shared<Rod>();
    c->Name        = Name;
    c->m_cube0Name = m_cube0Name;
    c->m_cube1Name = m_cube1Name;
    c->m_attachment0Name = m_attachment0Name;
    c->m_attachment1Name = m_attachment1Name;
    c->Color       = Color;
    c->LineWidth   = LineWidth;
    c->m_cube0     = m_cube0;
    c->m_cube1     = m_cube1;
    c->m_attachment0 = m_attachment0;
    c->m_attachment1 = m_attachment1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Rod::remapClonedInstances(const CloneRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = std::static_pointer_cast<BaseCube>(it->second); }
    if (auto a0 = m_attachment0.lock()) { auto it = map.find(a0.get()); if (it != map.end()) m_attachment0 = std::static_pointer_cast<Attachment>(it->second); }
    if (auto a1 = m_attachment1.lock()) { auto it = map.find(a1.get()); if (it != map.end()) m_attachment1 = std::static_pointer_cast<Attachment>(it->second); }
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
    } else if (name == "Attachment0") {
        m_attachment0Name = value.as<std::string>();
        m_attachment0.reset(); // 名前変更後に registerIfReady() 経由で再解決させる
    } else if (name == "Attachment1") {
        m_attachment1Name = value.as<std::string>();
        m_attachment1.reset();
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
    registerIfReady();
}

void Rod::onAncestorChanged() {
    Instance* ws_raw = findFirstAncestorWorkspace();
    if (ws_raw) {
        Workspace* ws = static_cast<Workspace*>(ws_raw);
        ws->registerConstraint(shared_from_this());
        m_lastWorkspace = ws;
    } else {
        if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_constraintHandle) {
            m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
        }
        m_lastWorkspace = nullptr;
    }
    Instance::onAncestorChanged();
}
