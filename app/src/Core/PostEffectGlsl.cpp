#include <Core/PostEffectGlsl.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace PostEffectGlsl {
namespace {

enum class TokenKind {
    Identifier,
    Preprocessor,
    Punctuation,
};

struct Token {
    TokenKind kind;
    std::string_view text;
    std::size_t begin;
    std::size_t end;
    std::size_t line;
};

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> tokens;
    std::size_t position = 0;
    std::size_t line = 1;
    bool lineStart = true;

    while (position < source.size()) {
        const char c = source[position];
        if (c == '\n') {
            ++position;
            ++line;
            lineStart = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++position;
            continue;
        }

        if (c == '/' && position + 1 < source.size() && source[position + 1] == '/') {
            position += 2;
            while (position < source.size() && source[position] != '\n') ++position;
            continue;
        }
        if (c == '/' && position + 1 < source.size() && source[position + 1] == '*') {
            position += 2;
            while (position + 1 < source.size()) {
                if (source[position] == '*' && source[position + 1] == '/') {
                    position += 2;
                    break;
                }
                if (source[position] == '\n') {
                    ++line;
                    lineStart = true;
                }
                ++position;
            }
            continue;
        }

        if (c == '#' && lineStart) {
            const std::size_t begin = position;
            while (position < source.size() && source[position] != '\n') ++position;
            tokens.push_back({TokenKind::Preprocessor, source.substr(begin, position - begin), begin, position, line});
            lineStart = false;
            continue;
        }

        lineStart = false;
        const std::size_t begin = position;
        if (isIdentifierStart(c)) {
            ++position;
            while (position < source.size() && isIdentifierPart(source[position])) ++position;
            tokens.push_back({TokenKind::Identifier, source.substr(begin, position - begin), begin, position, line});
            continue;
        }
        if (c == '"') {
            ++position;
            while (position < source.size()) {
                if (source[position] == '\\' && position + 1 < source.size()) {
                    position += 2;
                    continue;
                }
                if (source[position] == '"') {
                    ++position;
                    break;
                }
                if (source[position] == '\n') ++line;
                ++position;
            }
            tokens.push_back({TokenKind::Punctuation, source.substr(begin, position - begin), begin, position, line});
            continue;
        }

        ++position;
        tokens.push_back({TokenKind::Punctuation, source.substr(begin, 1), begin, position, line});
    }
    return tokens;
}

bool equals(const Token& token, std::string_view value) {
    return token.kind == TokenKind::Identifier && token.text == value;
}

bool isDeclaration(const std::vector<Token>& tokens, std::size_t index,
                   const std::vector<std::string_view>& expected) {
    if (index + expected.size() > tokens.size()) return false;
    std::size_t offset = 0;
    for (const std::string_view value : expected) {
        if (!equals(tokens[index + offset], value) && tokens[index + offset].text != value) return false;
        ++offset;
    }
    return true;
}

std::vector<std::string_view> preprocessorWords(std::string_view text) {
    std::vector<std::string_view> words;
    std::size_t position = 0;
    while (position < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[position])) != 0) {
            ++position;
            continue;
        }
        if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '/') break;
        if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '*') {
            position += 2;
            while (position + 1 < text.size() && !(text[position] == '*' && text[position + 1] == '/')) ++position;
            if (position + 1 < text.size()) position += 2;
            continue;
        }
        const std::size_t begin = position;
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) == 0 &&
               !(text[position] == '/' && position + 1 < text.size() &&
                 (text[position + 1] == '/' || text[position + 1] == '*'))) {
            ++position;
        }
        words.push_back(text.substr(begin, position - begin));
    }
    return words;
}

void maskRange(std::string& output, std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
        if (output[index] != '\n' && output[index] != '\r') output[index] = ' ';
    }
}

NormalizeResult failure(std::size_t line, std::string message) {
    NormalizeResult result;
    result.line = line;
    result.message = std::move(message);
    return result;
}

} // namespace

NormalizeResult normalize(std::string_view source) {
    if (source.empty()) return failure(1, "PostEffect GLSL source is empty");

    const std::vector<Token> tokens = tokenize(source);
    std::string transformed(source);
    bool versionFound = false;
    std::size_t versionLine = 1;
    for (const Token& token : tokens) {
        if (token.kind != TokenKind::Preprocessor) continue;
        const std::vector<std::string_view> words = preprocessorWords(token.text.substr(1));
        if (!words.empty() && words[0] == "version") {
            if (versionFound) return failure(token.line, "PostEffect GLSL contains more than one #version directive");
            versionFound = true;
            versionLine = token.line;
            if (words.size() != 3 || words[1] != "330" || words[2] != "core") {
                return failure(token.line, "PostEffect GLSL must use #version 330 core");
            }
            maskRange(transformed, token.begin, token.end);
        }
    }
    if (!versionFound) return failure(1, "PostEffect GLSL must declare #version 330 core");

    struct Required {
        std::vector<std::string_view> declaration;
        std::string_view name;
    };
    const std::array<Required, 8> required = {{
        {{"in", "vec2", "TexCoord", ";"}, "TexCoord"},
        {{"out", "vec4", "FragColor", ";"}, "FragColor"},
        {{"uniform", "sampler2D", "screenTexture", ";"}, "screenTexture"},
        {{"uniform", "vec2", "u_resolution", ";"}, "u_resolution"},
        {{"uniform", "float", "u_time", ";"}, "u_time"},
        {{"uniform", "float", "u_intensity", ";"}, "u_intensity"},
        {{"uniform", "float", "u_param1", ";"}, "u_param1"},
        {{"uniform", "float", "u_param2", ";"}, "u_param2"},
    }};

    std::array<int, 8> counts{};
    constexpr std::array<std::string_view, 8> publicNames = {
        "TexCoord", "FragColor", "screenTexture", "u_resolution",
        "u_time", "u_intensity", "u_param1", "u_param2"};
    std::size_t statementStart = 0;
    int braceDepth = 0;
    int parenthesisDepth = 0;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::Preprocessor) continue;
        const std::string_view tokenText = tokens[index].text;
        if (tokenText == "{") {
            if (braceDepth == 0 && parenthesisDepth == 0) statementStart = index + 1;
            ++braceDepth;
            continue;
        }
        if (tokenText == "}") {
            if (braceDepth > 0) --braceDepth;
            if (braceDepth == 0 && parenthesisDepth == 0) statementStart = index + 1;
            continue;
        }
        if (tokenText == "(") {
            ++parenthesisDepth;
            continue;
        }
        if (tokenText == ")") {
            if (parenthesisDepth > 0) --parenthesisDepth;
            continue;
        }
        if (tokenText != ";" || braceDepth != 0 || parenthesisDepth != 0) continue;

        const std::size_t statementEnd = index + 1;
        bool hasStorageQualifier = false;
        bool hasLayoutQualifier = false;
        for (std::size_t probe = statementStart; probe < statementEnd; ++probe) {
            if (tokens[probe].text == "uniform" || tokens[probe].text == "in" || tokens[probe].text == "out") {
                hasStorageQualifier = true;
            }
            if (tokens[probe].text == "layout") hasLayoutQualifier = true;
        }

        for (std::size_t requiredIndex = 0; requiredIndex < required.size(); ++requiredIndex) {
            bool found = false;
            for (std::size_t probe = statementStart; probe < statementEnd; ++probe) {
                if (probe + required[requiredIndex].declaration.size() > statementEnd) continue;
                if (!isDeclaration(tokens, probe, required[requiredIndex].declaration)) continue;
                found = true;
                if (hasLayoutQualifier) {
                    return failure(tokens[probe].line,
                                   "layout-qualified PostEffect declaration for " + std::string(publicNames[requiredIndex]) +
                                       " is not supported");
                }
                ++counts[requiredIndex];
                if (counts[requiredIndex] > 1) {
                    return failure(tokens[probe].line, "PostEffect GLSL declares " + std::string(required[requiredIndex].name) + " more than once");
                }
                maskRange(transformed, tokens[probe].begin,
                          tokens[probe + required[requiredIndex].declaration.size() - 1].end);
            }
            if (found) continue;

            for (std::size_t probe = statementStart; probe < statementEnd; ++probe) {
                if (tokens[probe].text != publicNames[requiredIndex]) continue;
                if (hasLayoutQualifier || hasStorageQualifier) {
                    return failure(tokens[probe].line,
                                   "PostEffect declaration for " + std::string(publicNames[requiredIndex]) +
                                       " has an invalid type or storage qualifier");
                }
            }
        }
        statementStart = statementEnd;
    }
    if (counts[1] == 0) {
        return failure(versionLine, "PostEffect GLSL is missing declaration for FragColor");
    }

    constexpr std::string_view header =
        "#version 450\n"
        "layout(location = 0) in vec2 TexCoord;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "layout(set = 2, binding = 0) uniform sampler2D screenTexture;\n"
        "layout(set = 3, binding = 0, std140) uniform PostEffectParams {\n"
        "    vec2 u_resolution;\n"
        "    float u_time;\n"
        "    float u_intensity;\n"
        "    float u_param1;\n"
        "    float u_param2;\n"
        "};\n"
        "#line 1\n";

    NormalizeResult result;
    result.success = true;
    result.source.reserve(header.size() + transformed.size());
    result.source.append(header);
    result.source.append(transformed);
    return result;
}

} // namespace PostEffectGlsl
