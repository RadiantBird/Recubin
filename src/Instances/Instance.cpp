#include "include/Instances/Instance.hpp"
#include <algorithm>
#include <vector>

void Instance::onAncestorChanged() {
    for (auto const& [_, child] : this->children) {
        child->onAncestorChanged();
    }
}

void Instance::setParent(Instance* newParent) {
    if (this->Parent == newParent) return;
    this->Parent = newParent;
    this->onAncestorChanged();
}

Instance* Instance::findFirstAncestorWorkspace() {
    Instance* current = this->Parent;
    while (current) {
        if (current->IsA("Workspace")) return current;
        current = current->Parent;
    }
    return nullptr;
}

Instance::Instance(string name) {
    this->Name = name;
}

std::string Instance::GetClassName() {
    return "Instance";
}

bool Instance::IsA(std::string className) {
    return className == GetClassName();
}

Instance* Instance::getChild(string child_name) {
    auto it = this->children.find(child_name);
    if (it != this->children.end()) {
        return it->second;
    }
    return nullptr;
}

const std::unordered_map<std::string, Instance*>& Instance::getChildren() {
    return this->children;
}

void Instance::addChild(Instance* child) {
    if (child == nullptr) {
        std::cout << "[WARN] addChild called but child is nullptr!\n";
        return;
    }
    child->Parent = this;
    child->onAncestorChanged();
    this->children[child->Name] = child;
}

bool Instance::removeChild(string name) {
    Instance* child = getChild(name);
    if (child) {
        Instance* parent = child->Parent;
        parent->children.erase(name);
        delete child;
        return true;
    }
    return false;
}

std::string Instance::getFullPath() {
    Instance* current = this;
    std::vector<string> data = {current->Name};

    while (current->Parent != nullptr) {
        current = current->Parent;
        data.push_back(current->Name);
    }

    std::reverse(data.begin(), data.end());

    std::string path = data[0];
    for (size_t i = 1; i < data.size(); i++) {
        path = path + "\\" + data[i];
    }

    if (data.size() == 1) {
        path += "\\";
    }

    return path;
}

Instance::~Instance() {
    for (auto const& [_, child] : this->children) {
        delete child;
    }
}
