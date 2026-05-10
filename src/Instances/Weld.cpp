#include <include/Instances/Weld.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <queue>
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
    if      (name == "Cube0") m_cube0Name = value.as<std::string>();
    else if (name == "Cube1") m_cube1Name = value.as<std::string>();
    else Instance::setProperty(name, value);
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
    std::vector<std::shared_ptr<BaseCube>> result;
    std::unordered_set<BaseCube*> visited;
    std::queue<std::shared_ptr<BaseCube>> queue;

    queue.push(start);
    visited.insert(start.get());

    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();
        result.push_back(current);

        // Workspace の children を走査して Weld を探す
        for (auto const& [name, child] : ws.children) {
            if (!child->IsA("Weld")) continue;
            auto weld = std::static_pointer_cast<Weld>(child);
            auto c0 = weld->m_cube0.lock();
            auto c1 = weld->m_cube1.lock();

            std::shared_ptr<BaseCube> neighbor;
            if (c0 == current && c1 && visited.find(c1.get()) == visited.end()) {
                neighbor = c1;
            } else if (c1 == current && c0 && visited.find(c0.get()) == visited.end()) {
                neighbor = c0;
            }

            if (neighbor) {
                visited.insert(neighbor.get());
                queue.push(neighbor);
            }
        }
    }

    return result;
}
