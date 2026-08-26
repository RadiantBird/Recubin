#include <Editor/SceneHierarchySelection.hpp>

#include <Instances/Instance.hpp>
#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace SceneHierarchySelection {

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
    return children;
}

} // namespace SceneHierarchySelection
