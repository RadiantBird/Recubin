#include <Editor/GuiAutomationCommand.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>

namespace {
std::vector<std::string> tokenize(std::string_view input) {
    std::vector<std::string> result;
    std::string token;
    bool quoted = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '"') { quoted = !quoted; continue; }
        if (c == '\\' && quoted && i + 1 < input.size() &&
            (input[i + 1] == '\\' || input[i + 1] == '"')) {
            token += input[++i];
        } else if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
            if (!token.empty()) { result.push_back(std::move(token)); token.clear(); }
        } else token += c;
    }
    if (quoted) return {};
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}
bool keySyntax(std::string_view value) {
    if (value.empty() || value.back() == '+') return false;
    size_t begin = 0;
    while (begin < value.size()) {
        const size_t plus = value.find('+', begin);
        const std::string_view rawPart = value.substr(begin,
            plus == std::string_view::npos ? std::string_view::npos : plus - begin);
        std::string normalized(rawPart);
        for (char& c : normalized) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string_view part = normalized;
        if (part.empty()) return false;
        if (plus == std::string_view::npos) {
            if (part.size() == 1 || part == "enter" || part == "esc" || part == "escape" ||
                part == "tab" || part == "backspace" || part == "delete" || part == "left" ||
                part == "right" || part == "up" || part == "down") return true;
            if (part.size() >= 2 && part[0] == 'f') {
                int number = 0;
                for (size_t i = 1; i < part.size(); ++i) {
                    if (part[i] < '0' || part[i] > '9') return false;
                    number = number * 10 + part[i] - '0';
                }
                return number >= 1 && number <= 12;
            }
            return false;
        }
        if (part != "ctrl" && part != "control" && part != "shift" && part != "alt" &&
            part != "super" && part != "cmd") return false;
        begin = plus + 1;
    }
    return false;
}

bool unsignedValue(std::string_view value) {
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool finiteFloat(std::string_view value) {
    std::string copy(value);
    char* end = nullptr;
    const float parsed = std::strtof(copy.c_str(), &end);
    return end == copy.data() + copy.size() && std::isfinite(parsed);
}
}

std::optional<GuiAutomationCommand> parseGuiAutomationCommand(std::string_view command) {
    const auto tokens = tokenize(command);
    if (tokens.empty()) return std::nullopt;
    const auto& name = tokens.front();
    if (name == "help" || name == "targets" || name == "quit") {
        if (tokens.size() != 1) return std::nullopt;
    }
    else if (name == "move" || name == "click" || name == "right_click" || name == "focus_window" || name == "capture") {
        if (tokens.size() != 2) return std::nullopt;
    } else if (name == "type") { if (tokens.size() < 2) return std::nullopt;
    } else if (name == "key") { if (tokens.size() != 2 || !keySyntax(tokens[1])) return std::nullopt;
    } else if (name == "mouse" || name == "wheel") {
        if (tokens.size() != 3 || !finiteFloat(tokens[1]) || !finiteFloat(tokens[2])) return std::nullopt;
    } else if (name == "mouse_down" || name == "mouse_up") {
        if (tokens.size() != 2 || (tokens[1] != "left" && tokens[1] != "right" && tokens[1] != "middle")) return std::nullopt;
    } else if (name == "wait") {
        if ((tokens.size() != 2 && tokens.size() != 3) || (tokens.size() == 3 && !unsignedValue(tokens[2]))) return std::nullopt;
    } else return std::nullopt;
    return GuiAutomationCommand{name, {tokens.begin() + 1, tokens.end()}};
}

bool validateGuiAutomationCommand(std::string_view command) {
    return parseGuiAutomationCommand(command).has_value();
}
