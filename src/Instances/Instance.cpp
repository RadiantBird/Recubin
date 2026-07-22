#include "include/Instances/Instance.hpp"
#include "include/Util/Logger.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <vector>

#ifdef _WIN32
    #undef getClassName // Windowsの勝手な置換をここで無効化する
#endif

// TODO: 無駄な処理を最適化する

// base と衝突しない名前を parent->children の中から探す（base, base1, base2, ...）。
// System::addChild() の Workspace 用ロジックと同じ命名規則。
static std::string uniqueChildName(const Instance& parent, const std::string& base) {
    std::string candidate = base;
    int suffix = 1;
    while (parent.children.count(candidate) > 0) {
        candidate = base + std::to_string(suffix++);
    }
    return candidate;
}

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

    if (newParent && m_runtimeNameLocked) {
        auto collision = newParent->children.find(Name);
        if (collision != newParent->children.end() && collision->second.get() != this) {
            RCBN_ERROR("setParent failed: canonical runtime name collision for '" << Name << "'");
            return;
        }
    }

    // 古い親のリストから自分を削除
    if (currentParent) {
        currentParent->children.erase(this->Name);
    }

    this->Parent = newParent;

    // 新しい親のリストに自分を追加
    if (newParent) {
        auto existingIt = newParent->children.find(this->Name);
        if (existingIt != newParent->children.end() && existingIt->second.get() != this) {
            std::string original = this->Name;
            this->Name = uniqueChildName(*newParent, original);
            RCBN_WARN("setParent: Key collision for '" << original << "' in " << newParent->Name
                      << ". Renamed new child to '" << this->Name << "' to avoid overwriting existing instance.");
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

Instance* Instance::findFirstAncestorSystem() {
    auto current = this->Parent.lock();
    while (current) {
        if (current->IsA("System")) return current.get();
        current = current->Parent.lock();
    }
    return nullptr;
}

// stopAt(除外)までの "\\" 区切り相対パスを作る（getChildByPath と対になる形式）
std::string Instance::getPathUpTo(Instance* stopAt) {
    std::vector<std::string> parts;
    Instance* cur = this;
    while (cur) {
        auto par = cur->Parent.lock();
        parts.push_back(cur->Name);
        if (!par || par.get() == stopAt) break;
        cur = par.get();
    }
    std::reverse(parts.begin(), parts.end());
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); i++) result += "\\" + parts[i];
    return result;
}

// 相対パスを返す。Workspace 配下なら Workspace 相対（例: "FolderA\CubeName"）、
// Workspace 外（StarterCharacter 等）なら最上位祖先(System 等)相対（例: "StarterCharacter\Head"）。
// resolveConstraintRefs / Weld::setProperty 側の解決規約と一致させる。
std::string Instance::getWorkspaceRelativePath() {
    Instance* stopAt = findFirstAncestorWorkspace();
    if (!stopAt) {
        // Workspace 外: 最上位の祖先（System 等）を起点にする
        Instance* top = this;
        for (auto p = Parent.lock(); p; p = p->Parent.lock()) top = p.get();
        stopAt = top;
    }
    return getPathUpTo(stopAt);
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
        renameTo(value.as<std::string>());
    }
}

void Instance::renameTo(const std::string& newName) {
    if (m_runtimeNameLocked) {
        RCBN_WARN("renameTo rejected for runtime-locked instance '" << Name << "'");
        return;
    }
    if (this->Name == newName) return;

    auto parent = this->Parent.lock();
    if (!parent) {
        this->Name = newName;
        return;
    }

    std::string finalName = newName;
    auto existingIt = parent->children.find(newName);
    if (existingIt != parent->children.end() && existingIt->second.get() != this) {
        finalName = uniqueChildName(*parent, newName);
        RCBN_WARN("renameTo: Key collision for '" << newName << "' in " << parent->Name
                  << ". Renamed to '" << finalName << "' to avoid overwriting existing instance.");
    }

    // shared_ptr を事前に取得して reference count を保つ
    // erase() で this が唯一の shared_ptr だった場合、デストラクタから保護する
    auto self = shared_from_this();
    parent->children.erase(this->Name);
    this->Name = finalName;
    parent->children[finalName] = self;
}

bool Instance::renameToAuthoritative(const std::string& newName) {
    if (newName.empty()) return false;
    if (Name == newName) return true;
    auto parent = Parent.lock();
    if (!parent) {
        Name = newName;
        return true;
    }
    auto collision = parent->children.find(newName);
    if (collision != parent->children.end() && collision->second.get() != this) {
        RCBN_ERROR("authoritative rename failed: canonical name collision for '" << newName << "'");
        return false;
    }
    auto self = shared_from_this();
    parent->children.erase(Name);
    Name = newName;
    parent->children.emplace(Name, std::move(self));
    return true;
}

std::shared_ptr<Instance> Instance::clone() const {
    auto copy = std::make_shared<Instance>(this->Name);
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

// orig と clone を子名で並行走査して Instance の対応表を作り（パス1）、
// clone ツリーの各ノードに remapClonedInstances を呼んで参照を張り替える（パス2）。
// 対応表は型を問わず全ノードを含むため、Instance は自分の派生クラスを一切知らない。

void Instance::rebindClonedConstraints(const Instance& orig, Instance& clone) {
    CloneRemap map;

    std::function<void(const Instance&, Instance&)> buildMap =
        [&](const Instance& o, Instance& c) {
            for (auto const& [name, oc] : o.children) {
                auto it = c.children.find(name);
                if (it == c.children.end() || !it->second || !oc) continue;
                map[oc.get()] = it->second;
                buildMap(*oc, *it->second);
            }
        };
    buildMap(orig, clone);

    std::function<void(Instance&)> applyRemap = [&](Instance& c) {
        c.remapClonedInstances(map);
        for (auto const& [name, ch] : c.children)
            if (ch) applyRemap(*ch);
    };
    applyRemap(clone);
}

std::shared_ptr<Instance> Instance::cloneTree() const {
    auto copies = cloneForest({std::const_pointer_cast<Instance>(shared_from_this())});
    return copies.empty() ? nullptr : copies.front();
}

std::vector<std::shared_ptr<Instance>> Instance::cloneForest(
    const std::vector<std::shared_ptr<Instance>>& roots) {
    std::vector<std::shared_ptr<Instance>> copies;
    CloneRemap map;

    std::function<void(const Instance&, const std::shared_ptr<Instance>&)> buildMap =
        [&](const Instance& original, const std::shared_ptr<Instance>& copy) {
            if (!copy) return;
            map[const_cast<Instance*>(&original)] = copy;
            for (auto const& [name, originalChild] : original.children) {
                auto it = copy->children.find(name);
                if (originalChild && it != copy->children.end()) buildMap(*originalChild, it->second);
            }
        };

    for (const auto& root : roots) {
        if (!root) continue;
        auto copy = root->clone();
        if (!copy) continue;
        buildMap(*root, copy);
        copies.push_back(std::move(copy));
    }

    std::function<void(Instance&)> applyRemap = [&](Instance& copy) {
        copy.remapClonedInstances(map);
        for (auto const& [name, child] : copy.children)
            if (child) applyRemap(*child);
    };
    for (const auto& copy : copies)
        if (copy) applyRemap(*copy);
    return copies;
}

Instance::~Instance() {
    assert(Parent.expired() && "Instance deleted while still owned by a parent.");
    this->children.clear();
}
