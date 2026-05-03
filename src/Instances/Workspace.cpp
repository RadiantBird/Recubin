#include <Instances/Workspace.hpp>
#include <algorithm>

void Workspace::registerScript(Instance* s) {
    scripts.push_back(s);
}

void Workspace::unregisterScript(Instance* s) {
    scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
}

void Workspace::registerCube(Instance* c) {
    if (std::find(pendingInstances.begin(), pendingInstances.end(), c) == pendingInstances.end()) {
        pendingInstances.push_back(c);
    }
}

Workspace::Workspace() : Instance("Workspace") {}

Workspace::~Workspace() {
    // children を先に破棄し、子のデストラクタ（BaseCube::~BaseCubeなど）が
    // Workspaceのメンバ（scripts, pendingInstances等）破棄前に呼ばれることを保証する
    this->children.clear();
}

std::string Workspace::GetClassName() {
    return "Workspace";
}

bool Workspace::IsA(std::string className) {
    if (className == "Workspace") {
        return true;
    }
    return Instance::IsA(className);
}

