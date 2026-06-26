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

void Rod::setCube0(std::shared_ptr<BaseCube> cube) {
    m_cube0 = cube;
    m_cube0Name = cube ? cube->Name : "";
    registerIfReady();
}

void Rod::setCube1(std::shared_ptr<BaseCube> cube) {
    m_cube1 = cube;
    m_cube1Name = cube ? cube->Name : "";
    registerIfReady();
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
    if (m_cube0.lock() && m_cube1.lock())
        ws->registerConstraint(shared_from_this());
}

std::shared_ptr<Instance> Rod::clone() const {
    auto c = std::make_shared<Rod>();
    c->Name        = Name;
    c->m_cube0Name = m_cube0Name;
    c->m_cube1Name = m_cube1Name;
    c->Color       = Color;
    c->LineWidth   = LineWidth;
    c->m_cube0     = m_cube0;
    c->m_cube1     = m_cube1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Rod::remapClonedCubes(const CubeRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = it->second; }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = it->second; }
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
    registerIfReady();
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
