#include <Editor/SceneHierarchySelection.hpp>

#include <Instances/Instance.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
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
    if (className == "Workspace") return 0;
    if (className == "Folder") return 1;
    if (className == "Model") return 2;
    if (className == "Script") return 3;
    if (className == "LocalScript") return 4;
    if (className == "ModuleScript") return 5;
    if (instance.IsA("PhysicalFileInstance")) return 6;
    if (instance.IsA("ValueBase")) return 7;
    if (instance.IsA("BaseCube")) return 8;
    return 9;
}

bool explorerLess(Instance* lhs, Instance* rhs) {
    if (lhs == rhs) return false;
    if (!lhs) return false;
    if (!rhs) return true;

    const int lhsPriority = explorerPriority(*lhs);
    const int rhsPriority = explorerPriority(*rhs);
    if (lhsPriority != rhsPriority) return lhsPriority < rhsPriority;

    const std::string lhsClass = lhs->getClassName();
    const std::string rhsClass = rhs->getClassName();
    const int classComparison = compareAsciiNatural(lhsClass, rhsClass);
    if (classComparison != 0) return classComparison < 0;

    const int nameComparison = compareAsciiNatural(lhs->Name, rhs->Name);
    if (nameComparison != 0) return nameComparison < 0;

    return std::less<Instance*>{}(lhs, rhs);
}

} // namespace

void sortForExplorer(std::vector<Instance*>& instances) {
    std::sort(instances.begin(), instances.end(), explorerLess);
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

} // namespace SceneHierarchySelection
