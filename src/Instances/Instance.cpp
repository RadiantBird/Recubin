#include "include/Instances/Instance.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <cassert>
#include <vector>

#ifdef _WIN32
    #undef getClassName // Windowsの勝手な置換をここで無効化する
#endif

// TODO: 無駄な処理を最適化する

void Instance::onAncestorChanged() {
    for (auto const& [_, child] : this->children) {
        child->onAncestorChanged();
    }
}

void Instance::setParent(std::shared_ptr<Instance> newParent) {
    auto currentParent = this->Parent.lock();
    if (currentParent == newParent) return;

    // 循環参照の防止（親が自分自身や自分の子孫にならないか）
    std::shared_ptr<Instance> check = newParent;
    while (check != nullptr) {
        if (check.get() == this) {
            RCBN_ERROR("setParent failed: Circular reference detected! Cannot set " << this->Name << " as child of its own descendant.");
            return;
        }
        check = check->Parent.lock();
    }

    // 古い親のリストから自分を削除
    if (currentParent) {
        currentParent->children.erase(this->Name);
    }

    this->Parent = newParent;

    // 新しい親のリストに自分を追加
    if (newParent) {
        auto existingIt = newParent->children.find(this->Name);
        if (existingIt != newParent->children.end()) {
            RCBN_WARN("setParent: Key collision for '" << this->Name << "' in " << newParent->Name << ". Overwriting existing child.");
            existingIt->second->Parent = {};
        }
        newParent->children[this->Name] = shared_from_this();
    }

    this->onAncestorChanged();
}

Instance* Instance::findFirstAncestorWorkspace() {
    auto current = this->Parent.lock();
    while (current) {
        if (current->IsA("Workspace")) return current.get();
        current = current->Parent.lock();
    }
    return nullptr;
}

Instance::Instance(string name) {
    this->Name = name;
}

std::string Instance::getClassName() {
    return "Instance";
}

bool Instance::IsA(std::string className) {
    if (className == "Instance") {
        return true;
    }
    return className == getClassName();
}

Instance* Instance::getChild(string child_name) {
    auto it = this->children.find(child_name);
    if (it != this->children.end()) {
        return it->second.get();
    }
    return nullptr;
}

Instance* Instance::getChildByPath(const std::string& path) {
    size_t sep = path.find('\\');
    if (sep == std::string::npos) return getChild(path);
    Instance* child = getChild(path.substr(0, sep));
    return child ? child->getChildByPath(path.substr(sep + 1)) : nullptr;
}

const std::unordered_map<std::string, std::shared_ptr<Instance>>& Instance::getChildren() {
    return this->children;
}

void Instance::addChild(std::shared_ptr<Instance> child) {
    if (child == nullptr) {
        RCBN_WARN("addChild called but child is nullptr!");
        return;
    }

    child->setParent(shared_from_this());
}

bool Instance::removeChild(string name) {
    auto it = this->children.find(name);
    if (it != this->children.end()) {
        auto child = it->second;
        child->Parent = {};
        this->children.erase(it);
        child->onAncestorChanged();
        return true;
    }
    return false;
}

std::string Instance::getFullPath() {
    std::vector<string> data = {this->Name};

    auto parent = this->Parent.lock();
    while (parent) {
        data.push_back(parent->Name);
        parent = parent->Parent.lock();
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
        string newName = value.as<std::string>();
        if (this->Name == newName) return;

        auto parent = this->Parent.lock();
        if (parent) {
            parent->children.erase(this->Name);
            this->Name = newName;
            parent->children[this->Name] = shared_from_this();
        } else {
            this->Name = newName;
        }
    }
}

std::shared_ptr<Instance> Instance::clone() const {
    auto copy = std::make_shared<Instance>(this->Name);
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

Instance::~Instance() {
    assert(Parent.expired() && "Instance deleted while still owned by a parent.");
    this->children.clear();
}
