#include <Util/InstanceOrdering.hpp>

#include <Instances/Instance.hpp>
#include <algorithm>
#include <cctype>
#include <string_view>

namespace InstanceOrdering {
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

int categoryPriority(std::string_view className,
                     bool isPhysicalFileInstance,
                     bool isValueBase,
                     bool isBaseCube) {
    if (className == "Users") return 0;
    if (className == "StarterCharacter") return 1;
    if (className == "Workspace") return 2;
    if (className == "Folder") return 3;
    if (className == "Model") return 4;
    if (className == "Script") return 5;
    if (className == "LocalScript") return 6;
    if (className == "ModuleScript") return 7;
    if (isPhysicalFileInstance) return 8;
    if (isValueBase) return 9;
    if (isBaseCube) return 10;
    return 11;
}

} // namespace

Key makeKey(Instance& instance) {
    const std::string className = instance.getClassName();
    const int directPriority = categoryPriority(className, false, false, false);
    if (directPriority != 11)
        return Key{directPriority, className, instance.Name};
    if (instance.IsA("PhysicalFileInstance"))
        return makeKey(className, instance.Name, true, false, false);
    if (instance.IsA("ValueBase"))
        return makeKey(className, instance.Name, false, true, false);
    if (instance.IsA("BaseCube"))
        return makeKey(className, instance.Name, false, false, true);
    return makeKey(className, instance.Name, false, false, false);
}

Key makeKey(std::string_view className, std::string_view name,
            bool isPhysicalFileInstance, bool isValueBase, bool isBaseCube) {
    return Key{
        categoryPriority(className, isPhysicalFileInstance, isValueBase, isBaseCube),
        std::string(className),
        name
    };
}

bool less(const Key& lhs, const Key& rhs) {
    if (lhs.categoryPriority != rhs.categoryPriority)
        return lhs.categoryPriority < rhs.categoryPriority;

    const int classComparison = compareAsciiNatural(lhs.className, rhs.className);
    if (classComparison != 0) return classComparison < 0;

    return compareAsciiNatural(lhs.name, rhs.name) < 0;
}

void sortForSerialization(std::vector<Instance*>& instances) {
    struct Entry {
        Instance* instance;
        Key key;
    };

    std::vector<Entry> entries;
    entries.reserve(instances.size());
    for (Instance* instance : instances) {
        if (!instance) {
            entries.push_back({nullptr, {}});
            continue;
        }
        entries.push_back({instance, makeKey(*instance)});
    }

    std::stable_sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) {
        if (!lhs.instance) return false;
        if (!rhs.instance) return true;
        return less(lhs.key, rhs.key);
    });

    for (std::size_t i = 0; i < entries.size(); ++i)
        instances[i] = entries[i].instance;
}

} // namespace InstanceOrdering
