#include "include/Instances/Instance.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
    #undef GetClassName // Windowsの勝手な置換をここで無効化する
#endif

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
    if (className == "Instance") {
        return true;
    }
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
        RCBN_WARN("addChild called but child is nullptr!");
        return;
    }

    // 重複キーの登録を禁止
    if (this->children.count(child->Name) > 0) {
        RCBN_WARN("addChild failed: Key collision for '" << child->Name << "' in " << this->Name);
        return;
    }

    child->Parent = this;
    this->children[child->Name] = child;
    child->onAncestorChanged();
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

void Instance::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Name") {
        this->Name = value.as<std::string>();
    }
}

Instance::~Instance() {
    RCBN_LOG("Instance Destructor: " << this->Name << " (" << this->GetClassName() << ")");
    // ループ中の予期せぬ不整合（子要素が親を触るなど）を防ぐため、コピーしたリストを破棄する
    std::vector<Instance*> toDelete;
    for (auto const& [_, child] : this->children) {
        toDelete.push_back(child);
    }
    this->children.clear();
    
    for (Instance* child : toDelete) {
        delete child;
    }
}
