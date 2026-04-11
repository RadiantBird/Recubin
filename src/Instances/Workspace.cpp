#include <include/Instances/Workspace.hpp>
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
