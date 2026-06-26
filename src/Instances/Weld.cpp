#include <include/Instances/Weld.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/Motor.hpp>
#include <include/Core/Physics.hpp>
#include <queue>
#include <set>
#include <unordered_set>

Weld::Weld()
    : Instance("Weld") {}

Weld::Weld(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1)
    : Instance("Weld"), m_cube0(cube0), m_cube1(cube1) {}

Weld::~Weld() {
    m_compound = nullptr;
}

void Weld::setCubes(std::shared_ptr<BaseCube> cube0, std::shared_ptr<BaseCube> cube1) {
    m_cube0 = cube0;
    m_cube1 = cube1;
}

void Weld::setCube0(std::shared_ptr<BaseCube> cube) {
    m_cube0 = cube;
    m_cube0Name = cube ? cube->Name : "";
    registerIfReady();
}

void Weld::setCube1(std::shared_ptr<BaseCube> cube) {
    m_cube1 = cube;
    m_cube1Name = cube ? cube->Name : "";
    registerIfReady();
}

void Weld::registerIfReady() {
    auto* ws_raw = findFirstAncestorWorkspace();
    if (!ws_raw) return;
    Workspace* ws = static_cast<Workspace*>(ws_raw);
    // 片方だけ名前で指定され未解決のCube(例: ロード時にCube1が空で、実行時にsetCube1された
    // ケース。SceneLoaderの解決パスは両Cubeが揃ったときだけ解決するため、もう片方は未設定の
    // まま残る)を、保存済みの名前から遅延解決する
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

std::string Weld::getClassName() { return "Weld"; }

bool Weld::IsA(std::string className) {
    if (className == "Weld") return true;
    return Instance::IsA(className);
}

void Weld::setProperty(const std::string& name, const YAML::Node& value) {
    // Workspace 配下なら Workspace 相対、そうでなければ最上位祖先(System 等)相対で解決する。
    // StarterCharacter 等 Workspace 外に置かれた溶接にも対応する。
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
        if (auto c = resolveCube(m_cube0Name)) m_cube0 = c;
    } else if (name == "Cube1") {
        m_cube1Name = value.as<std::string>();
        if (auto c = resolveCube(m_cube1Name)) m_cube1 = c;
    } else {
        Instance::setProperty(name, value);
    }
    registerIfReady();
}

std::shared_ptr<Instance> Weld::clone() const {
    auto c = std::make_shared<Weld>();
    c->Name        = Name;
    c->m_cube0Name = m_cube0Name;
    c->m_cube1Name = m_cube1Name;
    c->m_cube0     = m_cube0;   // 一旦は元キューブを指す（rebindClonedConstraints が張り替える）
    c->m_cube1     = m_cube1;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Weld::remapClonedCubes(const CubeRemap& map) {
    if (auto c0 = m_cube0.lock()) { auto it = map.find(c0.get()); if (it != map.end()) m_cube0 = it->second; }
    if (auto c1 = m_cube1.lock()) { auto it = map.find(c1.get()); if (it != map.end()) m_cube1 = it->second; }
}

void Weld::onAncestorChanged() {
    Instance* ws_raw = findFirstAncestorWorkspace();
    if (ws_raw) {
        Workspace* ws = static_cast<Workspace*>(ws_raw);
        ws->registerConstraint(shared_from_this());
        m_lastWorkspace = ws;
    } else {
        if (m_lastWorkspace && m_lastWorkspace->getPhysicsEngine() && m_compound) {
            m_lastWorkspace->getPhysicsEngine()->removeConstraint(shared_from_this());
        }
        m_lastWorkspace = nullptr;
    }
    Instance::onAncestorChanged();
}

std::vector<std::shared_ptr<BaseCube>>
Weld::collectAssembly(const std::shared_ptr<BaseCube>& start, const Instance& root) {
    // (1) Workspace 以下の全子孫から Weld / Motor を収集
    std::vector<std::shared_ptr<Weld>>  allWelds;
    std::vector<std::shared_ptr<Motor>> allMotors;
    auto collect = [&](auto& self, const Instance* inst) -> void {
        for (auto const& [n, c] : inst->children) {
            if (c->IsA("Weld"))  allWelds.push_back(std::static_pointer_cast<Weld>(c));
            if (c->IsA("Motor")) allMotors.push_back(std::static_pointer_cast<Motor>(c));
            self(self, c.get());
        }
    };
    collect(collect, &root);

    // (2) Motor で繋がるペアを越えてはいけない辺として登録
    std::set<std::pair<BaseCube*, BaseCube*>> motorBoundary;
    for (auto& m : allMotors) {
        auto mc0 = m->m_cube0.lock(), mc1 = m->m_cube1.lock();
        if (mc0 && mc1) {
            motorBoundary.insert({mc0.get(), mc1.get()});
            motorBoundary.insert({mc1.get(), mc0.get()});
        }
    }

    // (3) BFS — Motor 境界を越えない
    std::vector<std::shared_ptr<BaseCube>> result;
    std::unordered_set<BaseCube*> visited;
    std::queue<std::shared_ptr<BaseCube>> queue;
    queue.push(start);
    visited.insert(start.get());

    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();
        result.push_back(current);

        for (auto& weld : allWelds) {
            auto c0 = weld->m_cube0.lock();
            auto c1 = weld->m_cube1.lock();
            std::shared_ptr<BaseCube> neighbor;
            if (c0 == current && c1 && visited.find(c1.get()) == visited.end())
                neighbor = c1;
            else if (c1 == current && c0 && visited.find(c0.get()) == visited.end())
                neighbor = c0;

            if (neighbor && !motorBoundary.count({current.get(), neighbor.get()})) {
                visited.insert(neighbor.get());
                queue.push(neighbor);
            }
        }
    }

    return result;
}
