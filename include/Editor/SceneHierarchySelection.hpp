#pragma once

#include <vector>

class Instance;

namespace SceneHierarchySelection {

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

} // namespace SceneHierarchySelection
