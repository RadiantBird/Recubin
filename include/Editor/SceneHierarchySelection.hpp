#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

class Instance;

namespace SceneHierarchySelection {

// Sorts instances in the order used by the Explorer.  The order groups the
// important instance families first, then groups concrete classes within a
// family, and finally compares names using natural ASCII ordering.
void sortForExplorer(std::vector<Instance*>& instances);

// Selects the inclusive range between anchor and target in visible-order.
// If the anchor is not visible, target is used as a safe single-item range.
// append preserves the existing selection order and adds only missing items.
std::vector<Instance*> selectVisibleRange(
    const std::vector<Instance*>& visibleNodes,
    Instance* anchor,
    Instance* target,
    const std::vector<Instance*>& existingSelection,
    bool append);

// Returns only the parent's direct children in the same order used by Explorer.
std::vector<Instance*> collectDirectChildren(Instance& parent);

// Caches the Explorer order for each parent. The entry is rebuilt only when
// the parent's child/name revision changes.
class DirectChildrenCache {
public:
    const std::vector<Instance*>& get(Instance& parent);
    void clear();

private:
    struct Entry {
        std::uint64_t revision = 0;
        bool initialized = false;
        std::vector<Instance*> children;
    };

    std::unordered_map<Instance*, Entry> m_entries;
};

} // namespace SceneHierarchySelection
