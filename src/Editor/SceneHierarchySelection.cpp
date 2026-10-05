#include <Editor/SceneHierarchySelection.hpp>

#include <Instances/Instance.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>

namespace SceneHierarchySelection {

namespace {

int compareAsciiNatural(std::string_view lhs, std::string_view rhs) {
    size_t lhsIndex = 0;
    size_t rhsIndex = 0;

    while (lhsIndex < lhs.size() && rhsIndex < rhs.size()) {
        const unsigned char lhsChar = static_cast<unsigned char>(lhs[lhsIndex]);
        const unsigned char rhsChar = static_cast<unsigned char>(rhs[rhsIndex]);
        if (std::isdigit(lhsChar) && std::isdigit(rhsChar)) {
            const size_t lhsStart = lhsIndex;
            const size_t rhsStart = rhsIndex;
            while (lhsIndex < lhs.size() &&
                   std::isdigit(static_cast<unsigned char>(lhs[lhsIndex]))) {
                ++lhsIndex;
            }
            while (rhsIndex < rhs.size() &&
                   std::isdigit(static_cast<unsigned char>(rhs[rhsIndex]))) {
                ++rhsIndex;
            }

            size_t lhsSignificant = lhsStart;
            size_t rhsSignificant = rhsStart;
            while (lhsSignificant < lhsIndex && lhs[lhsSignificant] == '0') ++lhsSignificant;
            while (rhsSignificant < rhsIndex && rhs[rhsSignificant] == '0') ++rhsSignificant;

            const size_t lhsDigits = lhsIndex - lhsSignificant;
            const size_t rhsDigits = rhsIndex - rhsSignificant;
            if (lhsDigits != rhsDigits)
                return lhsDigits < rhsDigits ? -1 : 1;

            for (size_t offset = 0; offset < lhsDigits; ++offset) {
                const unsigned char lhsDigit =
                    static_cast<unsigned char>(lhs[lhsSignificant + offset]);
                const unsigned char rhsDigit =
                    static_cast<unsigned char>(rhs[rhsSignificant + offset]);
                if (lhsDigit != rhsDigit) return lhsDigit < rhsDigit ? -1 : 1;
            }

            // Equal numeric values still need a deterministic ASCII tie-break
            // for names such as Cube1 and Cube01.
            const std::string_view lhsRun = lhs.substr(lhsStart, lhsIndex - lhsStart);
            const std::string_view rhsRun = rhs.substr(rhsStart, rhsIndex - rhsStart);
            if (lhsRun != rhsRun)
                return lhsRun < rhsRun ? -1 : 1;
            continue;
        }

        if (lhs[lhsIndex] != rhs[rhsIndex])
            return lhsChar < rhsChar ? -1 : 1;
        ++lhsIndex;
        ++rhsIndex;
    }

    if (lhsIndex != lhs.size()) return 1;
    if (rhsIndex != rhs.size()) return -1;
    return 0;
}

int explorerPriority(Instance& instance) {
    const std::string className = instance.getClassName();
    if (className == "Users") return 0;
    if (className == "StarterCharacter") return 1;
    if (className == "Workspace") return 2;
    if (className == "Folder") return 3;
    if (className == "Model") return 4;
    if (className == "Script") return 5;
    if (className == "LocalScript") return 6;
    if (className == "ModuleScript") return 7;
    if (instance.IsA("PhysicalFileInstance")) return 8;
    if (instance.IsA("ValueBase")) return 9;
    if (instance.IsA("BaseCube")) return 10;
    return 11;
}

} // namespace

void sortForExplorer(std::vector<Instance*>& instances) {
    // 比較のたびに優先度とクラス名を計算すると、10万個で約170万回の文字列コピーと
    // IsA比較になる。キーを1回ずつ事前計算して、比較ではコピーも判定もしない。
    // 並びはexplorerLessと同じ: 優先度 → クラス名 → 名前 → ポインタ。nullは末尾。
    struct Key {
        Instance* instance;
        int priority;
        std::string className;
    };
    std::vector<Key> keys;
    keys.reserve(instances.size());
    for (Instance* instance : instances) {
        if (!instance) {
            keys.push_back({nullptr, 0, std::string()});
            continue;
        }
        keys.push_back({instance, explorerPriority(*instance), instance->getClassName()});
    }
    std::sort(keys.begin(), keys.end(), [](const Key& lhs, const Key& rhs) {
        if (lhs.instance == rhs.instance) return false;
        if (!lhs.instance) return false;
        if (!rhs.instance) return true;
        if (lhs.priority != rhs.priority) return lhs.priority < rhs.priority;
        if (lhs.className != rhs.className) {
            const int classComparison = compareAsciiNatural(lhs.className, rhs.className);
            if (classComparison != 0) return classComparison < 0;
        }
        const int nameComparison = compareAsciiNatural(lhs.instance->Name, rhs.instance->Name);
        if (nameComparison != 0) return nameComparison < 0;
        return std::less<Instance*>{}(lhs.instance, rhs.instance);
    });
    for (std::size_t i = 0; i < keys.size(); ++i) instances[i] = keys[i].instance;
}

std::vector<Instance*> selectVisibleRange(
    const std::vector<Instance*>& visibleNodes,
    Instance* anchor,
    Instance* target,
    const std::vector<Instance*>& existingSelection,
    bool append)
{
    std::vector<Instance*> result;
    std::unordered_set<Instance*> added;
    if (append) {
        result.reserve(existingSelection.size());
        for (Instance* inst : existingSelection) {
            if (inst && added.insert(inst).second) result.push_back(inst);
        }
    }

    if (!target) return result;

    const auto anchorIt = std::find(visibleNodes.begin(), visibleNodes.end(), anchor);
    const auto targetIt = std::find(visibleNodes.begin(), visibleNodes.end(), target);
    if (anchorIt == visibleNodes.end() || targetIt == visibleNodes.end()) {
        if (!append) result.clear();
        if (added.insert(target).second) result.push_back(target);
        return result;
    }

    auto first = anchorIt;
    auto last = targetIt;
    if (first > last) std::swap(first, last);
    result.reserve(result.size() + static_cast<size_t>(std::distance(first, last)) + 1);
    for (auto it = first; it != std::next(last); ++it) {
        Instance* inst = *it;
        if (inst && added.insert(inst).second) result.push_back(inst);
    }
    return result;
}

std::vector<Instance*> collectDirectChildren(Instance& parent) {
    std::vector<Instance*> children;
    children.reserve(parent.getChildren().size());
    for (const auto& [name, child] : parent.getChildren()) {
        if (child) children.push_back(child.get());
    }
    sortForExplorer(children);
    return children;
}

const std::vector<Instance*>& DirectChildrenCache::get(Instance& parent) {
    Entry& entry = m_entries[&parent];
    const std::uint64_t revision = parent.getChildrenRevision();
    const std::weak_ptr<Instance> owner = parent.weak_from_this();
    // 共有所有されていない(weak_from_thisが空)親は同一性を確認できないので毎回作り直す
    const bool sameOwner = !owner.expired() &&
        !entry.owner.owner_before(owner) && !owner.owner_before(entry.owner);
    if (!entry.initialized || !sameOwner || entry.revision != revision) {
        entry.children = collectDirectChildren(parent);
        entry.revision = revision;
        entry.owner = owner;
        entry.initialized = true;
    }
    return entry.children;
}

void DirectChildrenCache::purgeExpired() {
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.initialized && it->second.owner.expired())
            it = m_entries.erase(it);
        else
            ++it;
    }
}

void DirectChildrenCache::clear() {
    m_entries.clear();
}

} // namespace SceneHierarchySelection
