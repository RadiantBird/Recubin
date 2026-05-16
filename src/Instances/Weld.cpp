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

std::string Weld::GetClassName() { return "Weld"; }

bool Weld::IsA(std::string className) {
    if (className == "Weld") return true;
    return Instance::IsA(className);
}

void Weld::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else {
        Instance::setProperty(name, value);
    }
    if (m_cube0.lock() && m_cube1.lock()) {
        if (auto* ws_raw = findFirstAncestorWorkspace())
            static_cast<Workspace*>(ws_raw)->registerConstraint(shared_from_this());
    }
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
Weld::collectAssembly(const std::shared_ptr<BaseCube>& start, const Workspace& ws) {
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
    collect(collect, &ws);

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
