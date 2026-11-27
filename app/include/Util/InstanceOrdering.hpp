#pragma once

#include <string>
#include <string_view>
#include <vector>

class Instance;

namespace InstanceOrdering {

struct Key {
    int categoryPriority = 11;
    std::string className;
    std::string_view name; // Borrowed while the key is compared.
};

Key makeKey(Instance& instance);
Key makeKey(std::string_view className, std::string_view name,
            bool isPhysicalFileInstance, bool isValueBase, bool isBaseCube);
bool less(const Key& lhs, const Key& rhs);
void sortForSerialization(std::vector<Instance*>& instances);

} // namespace InstanceOrdering
