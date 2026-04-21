#include <Instances/Workspace.hpp>
#include <algorithm>

void Workspace::registerScript(Instance* s) {
    scripts.push_back(s);
}

void Workspace::unregisterScript(Instance* s) {
    scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
}

void Workspace::registerCube(Instance* c) {
    pendingInstances.push_back(c);
}

Workspace::Workspace() : Instance("Workspace") {}

Workspace::~Workspace() {
    // Workspaceのメンバ（scripts等）が破棄される前に、子要素を明示的に破棄する
    // これにより、子のデストラクタ内で unregisterScript 等が安全に呼ばれる
    std::vector<Instance*> toDelete;
    for (auto const& [_, child] : this->children) {
        toDelete.push_back(child);
    }
    this->children.clear();
    
    for (Instance* child : toDelete) {
        delete child;
    }
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

void Workspace::buildTestSpace() {
    // TODO
}
