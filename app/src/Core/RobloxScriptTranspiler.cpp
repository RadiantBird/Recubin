#include <Core/RobloxScriptTranspiler.hpp>

#include <Luau/Ast.h>
#include <Luau/Common.h>
#include <Luau/Parser.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RobloxScriptTranspiler {
namespace {

struct TextEdit {
    size_t begin = 0;
    size_t end = 0;
    std::string replacement;
};

class LineIndex {
public:
    explicit LineIndex(std::string_view text) : m_textSize(text.size()) {
        m_lineOffsets.push_back(0);
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                m_lineOffsets.push_back(i + 1);
            }
        }
    }

    bool toOffset(const Luau::Position& pos, size_t& out) const {
        if (pos.line >= m_lineOffsets.size()) {
            return false;
        }
        const size_t offset = m_lineOffsets[pos.line] + static_cast<size_t>(pos.column);
        if (offset > m_textSize) {
            return false;
        }
        out = offset;
        return true;
    }

    bool toRange(const Luau::Location& loc, size_t& outBegin, size_t& outEnd) const {
        if (!toOffset(loc.begin, outBegin) || !toOffset(loc.end, outEnd)) {
            return false;
        }
        return outBegin <= outEnd;
    }

private:
    size_t m_textSize = 0;
    std::vector<size_t> m_lineOffsets;
};

const std::unordered_map<std::string_view, std::string_view>& instanceClassMap() {
    static const std::unordered_map<std::string_view, std::string_view> table = {
        { "Part", "Cube" },
        { "MeshPart", "Cube" },

        // NOTE: This looks horrible. we should fix this because we don't have any shape builder!
        { "UnionOperation", "Cube" },
        { "IntersectOperation", "Cube" },
        // And this...
        { "CornerWedgePart", "Cube" },

        { "WedgePart", "Wedge" },
        { "TrussPart", "Truss" },
        { "VehicleSeat", "Seat" }, // NOTE: In recubin, Seat also act as VehicleSeat so its OK
        { "RemoteEvent", "Event" },
        { "BindableEvent", "Event" },
    };
    return table;
}

bool isKnownRecubinCreatableClass(std::string_view name) {
    static const std::unordered_set<std::string_view> known = {
        "Cube", "Cylinder", "TriangularPrism", "Wedge", "Truss", "Seat",
        "Sphere", "MeshCube", "LiquidCube", "SpawnLocation", "Skybox",
        "Model", "Folder", "Weld", "Motor6D", "Motor", "Rope", "Rod",
        "Spring", "PrismaticConstraint", "BallSocket", "NoCollision",
        "Attachment", "Force", "Gyro", "Decal", "Texture", "SurfaceMark",
        "Humanoid", "IntValue", "NumberValue", "BoolValue", "StringValue",
        "Vector3Value", "Color4Value", "CFrameValue", "QuaternionValue",
        "ObjectValue", "PointLight", "SpotLight", "Sound", "ParticleEmitter",
        "Highlight", "ProximityPrompt", "SurfaceGui", "BillboardGui",
        "TextLabel", "TextButton", "ImageLabel", "ImageButton",
        "Script", "LocalScript", "ModuleScript", "Event", "SignalEvent"
    };
    return known.find(name) != known.end();
}

std::string_view recubinClassName(std::string_view name) {
    if (name == "BasePart") {
        return "BaseCube";
    }
    return name;
}

// 元ソースの実際の開き引用符文字を使用して再現する。
// AST の quoteStyle は古い Luau バージョンでは常に QuotedSimple になる場合があるため、
// 元ソース文字を直接参照する方が確実。
std::string quoteStringLiteral(std::string_view value, char sourceQuoteChar) {
    const char quote = (sourceQuoteChar == '\'') ? '\'' : '"';
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back(quote);
    for (char c : value) {
        if (c == quote || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back(quote);
    return out;
}

class TranspilerVisitor final : public Luau::AstVisitor {
public:
    explicit TranspilerVisitor(const LineIndex& lines, std::string_view source)
        : m_lines(lines), m_source(source) {}

    const std::vector<TextEdit>& edits() const { return m_edits; }
    const std::vector<std::string>& warnings() const { return m_warnings; }

    bool visit(Luau::AstExprCall* node) override {
        if (auto* method = node->func->as<Luau::AstExprIndexName>()) {
            if (method->op == ':') {
                const std::string_view methodName = method->index.value;
                if (methodName == "IsA") {
                    rewriteRobloxClassArgument(node);
                }
                if (methodName == "GetPlayerFromCharacter") {
                    addWarning("Unsupported Instance method: :GetPlayerFromCharacter()");
                }
            }
        }

        if (auto* global = node->func->as<Luau::AstExprGlobal>()) {
            if (global->name.value == "tick") {
                addWarning("Unsupported global function: tick()");
            }
        }

        if (auto* idx = node->func->as<Luau::AstExprIndexName>()) {
            if (auto* global = idx->expr->as<Luau::AstExprGlobal>()) {
                const std::string_view gName = global->name.value;
                const std::string_view mName = idx->index.value;

                if (gName == "Instance" && idx->op == '.' && mName == "new") {
                    handleInstanceNew(node);
                    return true;
                }
                if (gName == "Color3" && idx->op == '.') {
                    if (handleColor3(node, idx->location, mName)) {
                        return false;
                    }
                    return true;
                }
                if (gName == "game" && node->self && mName == "GetService") {
                    if (handleGetService(node)) {
                        return false;
                    }
                    return true;
                }
            }
        }
        return true;
    }

    bool visit(Luau::AstExprIndexName* node) override {
        if (auto* global = node->expr->as<Luau::AstExprGlobal>()) {
            const std::string_view gName = global->name.value;
            const std::string_view idxName = node->index.value;

            if (gName == "game" && node->op == '.') {
                if (idxName == "Workspace") {
                    addEdit(node->location, "workspace");
                    return false;
                }
                if (idxName == "Lighting") {
                    addEdit(node->location, "workspace:FindChild(\"Lighting\")");
                    return false;
                }
                if (idxName != "GetService") {
                    addWarning("Unsupported game property: game." + std::string(idxName));
                }
            } else if (gName == "Enum" && node->op == '.') {
                if (idxName != "CursorType") {
                    addWarning("Unsupported Enum: Enum." + std::string(idxName));
                }
            }
        }

        if (node->op == ':') {
            const std::string_view method = node->index.value;
            if (method == "FindFirstChild") {
                addEdit(node->indexLocation, "FindChild");
            } else if (method == "WaitForChild") {
                addEdit(node->indexLocation, "WaitChild");
            } else if (method == "FindFirstChildOfClass" ||
                       method == "FindFirstChildWhichIsA" ||
                       method == "GetDescendants" ||
                       method == "GetFullName" ||
                       method == "GetAttribute" ||
                       method == "SetAttribute" ||
                       method == "TakeDamage") {
                addWarning("Unsupported Instance method: :" + std::string(method) + "()");
            } else if (method == "ClearAllChildren") {
                addWarning("Unsupported Instance method: :" + std::string(method) + "()");
            }
        } else if (node->op == '.' && node->index.value == "DescendantAdded") {
            addWarning("Unsupported Instance event: .DescendantAdded");
        }
        return true;
    }

private:
    const LineIndex& m_lines;
    std::string_view m_source;
    std::vector<TextEdit> m_edits;
    std::vector<std::string> m_warnings;
    std::unordered_set<std::string> m_seenWarnings;

    void addEdit(const Luau::Location& loc, std::string replacement) {
        size_t begin = 0;
        size_t end = 0;
        if (m_lines.toRange(loc, begin, end)) {
            m_edits.push_back(TextEdit{ begin, end, std::move(replacement) });
        }
    }

    void addWarning(const std::string& message) {
        if (m_seenWarnings.insert(message).second) {
            m_warnings.push_back(message);
        }
    }

    void handleInstanceNew(Luau::AstExprCall* node) {
        if (node->args.size >= 2) {
            addWarning("Instance.new second argument (parent) is not supported in Recubin");
        }
        if (node->args.size < 1) {
            return;
        }
        auto* str = node->args.data[0]->as<Luau::AstExprConstantString>();
        if (!str) {
            return;
        }
        const std::string_view className(str->value.data, str->value.size);
        const auto& map = instanceClassMap();
        if (const auto it = map.find(className); it != map.end()) {
            // 元ソースの開き引用符文字を取得して引用符スタイルを保持する。
            char sourceQuoteChar = '"';
            size_t strBegin = 0;
            if (m_lines.toOffset(str->location.begin, strBegin) && strBegin < m_source.size()) {
                const char ch = m_source[strBegin];
                if (ch == '\'' || ch == '"') {
                    sourceQuoteChar = ch;
                }
            }
            addEdit(str->location, quoteStringLiteral(it->second, sourceQuoteChar));
            return;
        }
        if (!isKnownRecubinCreatableClass(className)) {
            addWarning("Unsupported Instance.new(\"" + std::string(className) + "\")");
        }
    }

    bool handleColor3(Luau::AstExprCall* node, const Luau::Location& funcLoc, std::string_view method) {
        if (method == "new") {
            if (node->args.size == 0) {
                addEdit(node->location, "Color4.new(0, 0, 0)");
                return true;
            }
            addEdit(funcLoc, "Color4.new");
            return false;
        }
        if (method == "fromRGB") {
            if (node->args.size == 0) {
                addEdit(node->location, "Color4.fromRGBA(0, 0, 0)");
                return true;
            }
            addEdit(funcLoc, "Color4.fromRGBA");
            return false;
        }
        addWarning("Unsupported Color3 method: Color3." + std::string(method));
        return false;
    }

    bool handleGetService(Luau::AstExprCall* node) {
        if (node->args.size != 1) {
            return false;
        }
        auto* str = node->args.data[0]->as<Luau::AstExprConstantString>();
        if (!str) {
            return false;
        }
        const std::string_view service(str->value.data, str->value.size);
        if (service == "Workspace") {
            addEdit(node->location, "workspace");
            return true;
        }
        if (service == "RunService") {
            addEdit(node->location, "System");
            return true;
        }
        if (service == "Lighting") {
            addEdit(node->location, "workspace:FindChild(\"Lighting\")");
            return true;
        }
        if (service == "Chat") {
            addEdit(node->location, "ChatService");
            return true;
        }
        addWarning("Unsupported service: game:GetService(\"" + std::string(service) + "\")");
        return false;
    }

    void rewriteRobloxClassArgument(Luau::AstExprCall* node) {
        if (node->args.size < 1) {
            return;
        }

        auto* className = node->args.data[0]->as<Luau::AstExprConstantString>();
        if (!className) {
            return;
        }

        const std::string_view original(className->value.data, className->value.size);
        const std::string_view replacement = recubinClassName(original);
        if (replacement == original) {
            return;
        }

        char sourceQuoteChar = '"';
        size_t stringBegin = 0;
        if (m_lines.toOffset(className->location.begin, stringBegin) &&
            stringBegin < m_source.size()) {
            const char ch = m_source[stringBegin];
            if (ch == '\'' || ch == '"') {
                sourceQuoteChar = ch;
            }
        }
        addEdit(className->location, quoteStringLiteral(replacement, sourceQuoteChar));
    }
};

std::string applyEdits(const std::string& source, std::vector<TextEdit> edits) {
    if (edits.empty()) {
        return source;
    }
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
        if (a.begin != b.begin) return a.begin > b.begin;
        return a.end > b.end;
    });

    std::string output = source;
    size_t lastBegin = output.size();
    for (const TextEdit& edit : edits) {
        if (edit.end > lastBegin) {
            continue; // 重複・入れ子になった編集は外側を優先しスキップする
        }
        output.replace(edit.begin, edit.end - edit.begin, edit.replacement);
        lastBegin = edit.begin;
    }
    return output;
}

} // namespace

Result transpile(const std::string& source) {
    Result result;
    result.source = source;
    if (source.empty()) {
        return result;
    }

    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseOptions options;
    const Luau::ParseResult parsed = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);

    if (!parsed.errors.empty()) {
        for (const Luau::ParseError& err : parsed.errors) {
            result.warnings.push_back(
                "Luau parse error at line " + std::to_string(err.getLocation().begin.line + 1) +
                ": " + err.getMessage());
        }
        return result;
    }

    const LineIndex lines(source);
    TranspilerVisitor visitor(lines, source);
    if (parsed.root) {
        parsed.root->visit(&visitor);
    }

    result.source = applyEdits(source, visitor.edits());
    result.warnings = visitor.warnings();
    return result;
}

} // namespace RobloxScriptTranspiler
