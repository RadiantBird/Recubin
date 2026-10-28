#include <Core/RbxlxDocument.hpp>
#include <pugixml.hpp>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace RbxlxDocument {
namespace {

// 数値テキストの読み取り。読めない・非有限なら fallback を返す。
double parseNumber(const char* text, double fallback) {
    if (!text || !*text) return fallback;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value)) return fallback;
    return value;
}

float childFloat(const pugi::xml_node& node, const char* name) {
    return static_cast<float>(parseNumber(node.child_value(name), 0.0));
}

bool allFinite(const float* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) return false;
    }
    return true;
}

// <X><Y><Z><R00>..<R22> を CFrame にする。回転行列の要素が欠けていたり非有限なら nullopt。
std::optional<CFrame> parseCFrame(const pugi::xml_node& node) {
    static const char* const ROTATION_NAMES[9] = {
        "R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22" };
    float r[9];
    for (int i = 0; i < 9; ++i) {
        const pugi::xml_node element = node.child(ROTATION_NAMES[i]);
        if (!element) return std::nullopt;
        const char* text = element.child_value();
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        if (end == text) return std::nullopt;
        r[i] = static_cast<float>(value);
    }
    const float position[3] = {
        childFloat(node, "X"), childFloat(node, "Y"), childFloat(node, "Z") };
    if (!allFinite(r, 9) || !allFinite(position, 3)) return std::nullopt;

    // Roblox の R の行列は行優先（R00 R01 R02 が 1 行目）。Quaternion は列優先の 4x4 を受け取る。
    float m[16] = {};
    m[0] = r[0]; m[1] = r[3]; m[2] = r[6];
    m[4] = r[1]; m[5] = r[4]; m[6] = r[7];
    m[8] = r[2]; m[9] = r[5]; m[10] = r[8];
    m[15] = 1.0f;
    return CFrame(Vector3(position[0], position[1], position[2]), Quaternion::FromRotationMatrix(m));
}

// Color3uint8 は 0xAARRGGBB の整数。
Color3 parseColor3uint8(const char* text) {
    const unsigned long packed = std::strtoul(text ? text : "0", nullptr, 10);
    return Color3{
        static_cast<float>((packed >> 16) & 0xFF) / 255.0f,
        static_cast<float>((packed >> 8) & 0xFF) / 255.0f,
        static_cast<float>(packed & 0xFF) / 255.0f };
}

// 型タグごとに値を読む。対応しない型（BinaryString, SharedString など）は nullopt。
std::optional<Value> parseValue(const pugi::xml_node& node) {
    const std::string_view type = node.name();
    const char* text = node.child_value();

    if (type == "bool") return Value(std::string_view(text) == "true");
    if (type == "int" || type == "int64" || type == "float" || type == "double" || type == "token")
        return Value(parseNumber(text, 0.0));
    if (type == "string" || type == "ProtectedString") return Value(std::string(text));
    if (type == "Color3") {
        return Value(Color3{ childFloat(node, "R"), childFloat(node, "G"), childFloat(node, "B") });
    }
    if (type == "Color3uint8") return Value(parseColor3uint8(text));
    if (type == "Vector2") return Value(Vector2(childFloat(node, "X"), childFloat(node, "Y")));
    if (type == "Vector3") {
        return Value(Vector3(childFloat(node, "X"), childFloat(node, "Y"), childFloat(node, "Z")));
    }
    if (type == "CoordinateFrame") {
        auto cframe = parseCFrame(node);
        if (!cframe) return std::nullopt;
        return Value(*cframe);
    }
    if (type == "OptionalCoordinateFrame") {
        const pugi::xml_node inner = node.child("CFrame");
        if (!inner) return std::nullopt;
        auto cframe = parseCFrame(inner);
        if (!cframe) return std::nullopt;
        return Value(*cframe);
    }
    if (type == "Ref") {
        const std::string referent = text;
        return Value(Ref{ referent == "null" ? std::string() : referent });
    }
    if (type == "Content") {
        return Value(Content{ node.child_value("url") });
    }
    return std::nullopt;
}

std::unique_ptr<Node> parseItem(const pugi::xml_node& item, Node* parent) {
    auto node = std::make_unique<Node>();
    node->className = item.attribute("class").as_string();
    node->referent = item.attribute("referent").as_string();
    node->parent = parent;

    for (const pugi::xml_node& prop : item.child("Properties").children()) {
        const char* name = prop.attribute("name").as_string();
        if (!*name) continue;
        auto value = parseValue(prop);
        if (value) node->properties.push_back(Property{ name, std::move(*value) });
    }
    for (const pugi::xml_node& child : item.children("Item")) {
        node->children.push_back(parseItem(child, node.get()));
    }
    return node;
}

template <class T>
const T* findAs(const Node& node, const std::string& name) {
    const Value* value = node.find(name);
    return value ? std::get_if<T>(value) : nullptr;
}

} // namespace

const Value* Node::find(const std::string& name) const {
    for (const Property& property : properties) {
        if (property.name == name) return &property.value;
    }
    return nullptr;
}

bool Node::getBool(const std::string& name, bool fallback) const {
    const bool* value = findAs<bool>(*this, name);
    return value ? *value : fallback;
}

double Node::getNumber(const std::string& name, double fallback) const {
    const double* value = findAs<double>(*this, name);
    return value ? *value : fallback;
}

std::string Node::getString(const std::string& name) const {
    const std::string* value = findAs<std::string>(*this, name);
    return value ? *value : std::string();
}

std::optional<Color3> Node::getColor(const std::string& name) const {
    const Color3* value = findAs<Color3>(*this, name);
    return value ? std::optional<Color3>(*value) : std::nullopt;
}

std::optional<Vector2> Node::getVector2(const std::string& name) const {
    const Vector2* value = findAs<Vector2>(*this, name);
    return value ? std::optional<Vector2>(*value) : std::nullopt;
}

std::optional<Vector3> Node::getVector3(const std::string& name) const {
    const Vector3* value = findAs<Vector3>(*this, name);
    return value ? std::optional<Vector3>(*value) : std::nullopt;
}

std::optional<CFrame> Node::getCFrame(const std::string& name) const {
    const CFrame* value = findAs<CFrame>(*this, name);
    return value ? std::optional<CFrame>(*value) : std::nullopt;
}

std::string Node::getRef(const std::string& name) const {
    const Ref* value = findAs<Ref>(*this, name);
    return value ? value->referent : std::string();
}

std::string Node::getContent(const std::string& name) const {
    const Content* value = findAs<Content>(*this, name);
    return value ? value->url : std::string();
}

std::string Node::name() const {
    const std::string value = getString("Name");
    return value.empty() ? className : value;
}

std::optional<Document> load(const std::filesystem::path& path, std::string& error) {
    pugi::xml_document xml;
    const pugi::xml_parse_result parsed = xml.load_file(path.c_str());
    if (!parsed) {
        error = "XML parse failed at offset " + std::to_string(parsed.offset) + ": " + parsed.description();
        return std::nullopt;
    }
    const pugi::xml_node root = xml.child("roblox");
    if (!root) {
        error = "Not a Roblox XML place (missing <roblox> root)";
        return std::nullopt;
    }

    Document document;
    for (const pugi::xml_node& item : root.children("Item")) {
        document.roots.push_back(parseItem(item, nullptr));
    }
    return document;
}

} // namespace RbxlxDocument
