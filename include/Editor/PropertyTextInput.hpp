#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <limits>

namespace PropertyTextInput {

bool parseFloatList(std::string_view text, std::size_t count,
                    std::vector<float>& values);
std::string formatFloatList(const float* values, std::size_t count);

struct Result {
    bool submitted = false;
    bool invalid = false;
    bool suppressGenericTransaction = false;
    std::vector<float> values;
};

// Keeps an editable draft keyed by the current ImGui item ID. The draft is
// refreshed from the property only while the field is not being edited.
Result draw(const char* label, const float* values, std::size_t count,
            bool mixed = false,
            float minimum = -std::numeric_limits<float>::infinity(),
            float maximum = std::numeric_limits<float>::infinity());

} // namespace PropertyTextInput
