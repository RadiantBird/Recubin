#pragma once
#include <Math/CFrame.hpp>
#include <Math/Vector2.hpp>
#include <Math/Vector3.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Roblox の XML 形式プレイス (.rbxlx) を読み込み、型付きプロパティを持つノードツリーにする。
// Recubin のクラスへの対応付けは RbxlxConverter が行う（ここは読み込みだけを担当する）。
namespace RbxlxDocument {

struct Color3 { float r = 0, g = 0, b = 0; };
struct Ref { std::string referent; };           // "null" は空文字にする
struct Content { std::string url; };            // <null> は空文字にする

// bool / 数値(int,float,double,token,int64) / 文字列(string,ProtectedString) / その他
using Value = std::variant<bool, double, std::string, Color3, Vector2, Vector3, CFrame, Ref, Content>;

struct Property {
    std::string name;
    Value value;
};

struct Node {
    std::string className;
    std::string referent;
    std::vector<Property> properties;
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    const Value* find(const std::string& name) const;
    bool getBool(const std::string& name, bool fallback) const;
    double getNumber(const std::string& name, double fallback) const;
    std::string getString(const std::string& name) const;
    std::optional<Color3> getColor(const std::string& name) const;
    std::optional<Vector2> getVector2(const std::string& name) const;
    std::optional<Vector3> getVector3(const std::string& name) const;
    std::optional<CFrame> getCFrame(const std::string& name) const;
    std::string getRef(const std::string& name) const;
    std::string getContent(const std::string& name) const;
    std::string name() const;   // Name プロパティ（無ければ className）
};

struct Document {
    std::vector<std::unique_ptr<Node>> roots;   // <roblox> 直下の Item（サービス）
};

// 失敗時は nullopt を返し、error に理由を入れる。
std::optional<Document> load(const std::filesystem::path& path, std::string& error);

} // namespace RbxlxDocument
