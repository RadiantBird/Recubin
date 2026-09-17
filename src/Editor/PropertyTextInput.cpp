#include <Editor/PropertyTextInput.hpp>

#include <include/imgui/imgui.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace PropertyTextInput {
namespace {
struct Draft {
    std::string text;
    bool active = false;
    bool invalid = false;
    bool committed = false;
};

std::unordered_map<ImGuiID, Draft> drafts;

bool isSeparator(char value) {
    return value == ',' || value == ' ' || value == '\t' ||
           value == '\r' || value == '\n';
}
} // namespace

bool parseFloatList(std::string_view text, std::size_t count,
                    std::vector<float>& values) {
    const std::string input(text);
    values.clear();
    std::size_t offset = 0;
    while (offset < input.size() && isSeparator(input[offset])) ++offset;
    while (offset < input.size()) {
        const char* begin = input.data() + offset;
        char* end = nullptr;
        const float value = std::strtof(begin, &end);
        if (end == begin || !std::isfinite(value)) return false;
        const std::size_t consumed = static_cast<std::size_t>(end - begin);
        values.push_back(value);
        if (values.size() > count) return false;
        offset += consumed;
        while (offset < input.size() && isSeparator(input[offset])) ++offset;
    }
    if (values.empty()) return false;
    values.resize(count, values.back());
    return true;
}

std::string formatFloatList(const float* values, std::size_t count) {
    if (!values) return {};
    std::string result;
    std::array<char, 64> buffer{};
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) result += ", ";
        std::snprintf(buffer.data(), buffer.size(), "%.6g", values[index]);
        result += buffer.data();
    }
    return result;
}

Result draw(const char* label, const float* values, std::size_t count,
            bool mixed, float minimum, float maximum) {
    Result result;
    if (!label || !values || count == 0) return result;

    const ImGuiID id = ImGui::GetID(label);
    Draft& draft = drafts[id];
    if (!draft.active) {
        draft.text = mixed ? std::string{} : formatFloatList(values, count);
        draft.invalid = false;
    }

    std::array<char, 512> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", draft.text.c_str());
    const bool submitted = ImGui::InputTextWithHint(
        label, mixed ? "Mixed value" : "x, y, z", buffer.data(), buffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    draft.text = buffer.data();
    draft.active = ImGui::IsItemActive();
    result.submitted = submitted;
    if (submitted) {
        result.invalid = !parseFloatList(draft.text, count, result.values);
        if (!result.invalid) {
            for (float value : result.values) {
                if (value < minimum || value > maximum) {
                    result.invalid = true;
                    break;
                }
            }
        }
        if (!result.invalid) {
            draft.text = formatFloatList(result.values.data(), result.values.size());
        }
        draft.invalid = result.invalid;
        // An Enter commit owns its undo transaction. Keep suppressing the
        // inspector's drag transaction through the later focus deactivation.
        draft.committed = true;
    } else {
        result.invalid = draft.invalid;
    }
    result.suppressGenericTransaction = draft.committed;
    if (!draft.active) draft.committed = false;
    if (draft.invalid) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Invalid");
    }
    return result;
}
} // namespace PropertyTextInput
