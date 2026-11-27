#include <Editor/SceneHierarchySelection.hpp>

#include <Instances/Instance.hpp>
#include <Util/InstanceOrdering.hpp>
#include <algorithm>
#include <functional>
#include <iterator>
#include <unordered_set>

namespace SceneHierarchySelection {

void sortForExplorer(std::vector<Instance*>& instances) {
    // 比較のたびに優先度とクラス名を計算すると、10万個で約170万回の文字列コピーと
    // IsA比較になる。キーを1回ずつ事前計算して、比較ではコピーも判定もしない。
    // 並びはexplorerLessと同じ: 優先度 → クラス名 → 名前 → ポインタ。nullは末尾。
    struct Key {
        Instance* instance;
        InstanceOrdering::Key order;
    };
    std::vector<Key> keys;
    keys.reserve(instances.size());
    for (Instance* instance : instances) {
        if (!instance) {
            keys.push_back({nullptr, {}});
            continue;
        }
        keys.push_back({instance, InstanceOrdering::makeKey(*instance)});
    }
    std::sort(keys.begin(), keys.end(), [](const Key& lhs, const Key& rhs) {
        if (lhs.instance == rhs.instance) return false;
        if (!lhs.instance) return false;
        if (!rhs.instance) return true;
        if (InstanceOrdering::less(lhs.order, rhs.order)) return true;
        if (InstanceOrdering::less(rhs.order, lhs.order)) return false;
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
