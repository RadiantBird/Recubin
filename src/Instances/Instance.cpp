#include "include/Instances/Instance.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <cassert>
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

    // 循環参照の防止（親が自分自身や自分の子孫にならないか）
    Instance* check = newParent;
    while (check != nullptr) {
        if (check == this) {
            RCBN_ERROR("setParent failed: Circular reference detected! Cannot set " << this->Name << " as child of its own descendant.");
            return;
        }
        check = check->Parent;
    }

    // 古い親のリストから自分を削除
    if (this->Parent != nullptr) {
        this->Parent->children.erase(this->Name);
    }

    // 新しい親をセット
    this->Parent = newParent;

    // 新しい親のリストに自分を追加
    if (this->Parent != nullptr) {
        // 同名の子がいる場合は上書きされる（警告を出すのが親切）
        if (this->Parent->children.count(this->Name) > 0) {
            RCBN_WARN("setParent: Key collision for '" << this->Name << "' in " << this->Parent->Name << ". Overwriting existing child.");
        }
        this->Parent->children[this->Name] = this;
    }

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

    child->setParent(this);
}

bool Instance::removeChild(string name) {
    Instance* child = getChild(name);
    if (child) {
        this->children.erase(name);
        child->Parent = nullptr;
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
        string newName = value.as<std::string>();
        if (this->Name == newName) return;

        // 親がいる場合は、親のマップ内のキーを更新する必要がある
        if (this->Parent != nullptr) {
            this->Parent->children.erase(this->Name);
            this->Name = newName;
            this->Parent->children[this->Name] = this;
        } else {
            this->Name = newName;
        }
    }
}

Instance::~Instance() {
    // RCBN_LOG("Instance Destructor: " << this->Name << " (" << this->GetClassName() << ")");
    assert(Parent == nullptr && "Instance deleted directly while still owned by a parent. Use parent->removeChild() instead.");

    std::vector<Instance*> toDelete;
    for (auto const& [_, child] : this->children) {
        toDelete.push_back(child);
    }
    this->children.clear();
    for (Instance* child : toDelete) {
        child->Parent = nullptr;
        // Hello world! And in case I don’t see ya, good pointer, good borrowing, and goodbye world!
        delete child;
    }
}
