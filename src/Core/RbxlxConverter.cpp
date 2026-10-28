#include <Core/RbxlxConverter.hpp>
#include <Core/RbxlxDocument.hpp>
#include <Util/AtomicFile.hpp>
#include <Util/Logger.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace RbxlxConverter {
namespace {

using RbxNode = RbxlxDocument::Node;

constexpr float PI_F = 3.14159265358979f;

// =====================================================================
// 出力ツリー
// =====================================================================

struct PendingRef {
    std::string key;        // 出力するプロパティ名（Cube0, Part1, PrimaryCube など）
    std::string referent;   // Roblox 側の参照先
};

struct OutNode {
    std::string className;
    std::string name;
    YAML::Node properties = YAML::Node(YAML::NodeType::Map);
    std::vector<std::unique_ptr<OutNode>> children;
    OutNode* parent = nullptr;

    bool spatial = false;   // Position/Rotation を座標親からのローカル値で書き出す
    bool isCube = false;    // ジョイントの参照先として有効か
    CFrame world;           // spatial のときのワールド CFrame
    CFrame local;           // resolveTransforms で求める

    std::vector<PendingRef> refs;
    bool requireRefs = false;   // 参照が解決できなければ自身を捨てる（ジョイント）
    bool removed = false;
};

struct Context {
    fs::path scriptDir;
    std::unordered_map<std::string, OutNode*> byReferent;
    std::map<std::string, int> skippedClasses;   // 捨てたクラス → 件数
    std::map<std::string, int> notes;            // その他の警告 → 件数
    int skippedCount = 0;
    int scriptIndex = 0;
    const RbxNode* lighting = nullptr;
};

void addNote(Context& ctx, const std::string& note) { ++ctx.notes[note]; }

void addSkipped(Context& ctx, const std::string& className) {
    ++ctx.skippedClasses[className];
    ++ctx.skippedCount;
}

// 子孫を含めて捨てる。最上位だけをクラス名つきで数え、子孫は件数にだけ加える。
void skipSubtree(const RbxNode& rbx, Context& ctx) {
    addSkipped(ctx, rbx.className);
    auto countDescendants = [&ctx](auto& self, const RbxNode& node) -> void {
        for (const auto& child : node.children) {
            ++ctx.skippedCount;
            self(self, *child);
        }
    };
    countDescendants(countDescendants, rbx);
}

std::unique_ptr<OutNode> makeNode(const char* className, const RbxNode& rbx) {
    auto node = std::make_unique<OutNode>();
    node->className = className;
    node->name = rbx.name();
    return node;
}

// =====================================================================
// YAML の値
// =====================================================================

YAML::Node flowSequence(std::initializer_list<float> values) {
    YAML::Node sequence(YAML::NodeType::Sequence);
    for (float value : values) sequence.push_back(value);
    sequence.SetStyle(YAML::EmitterStyle::Flow);
    return sequence;
}

YAML::Node toYaml(const Vector3& v) { return flowSequence({ v.x, v.y, v.z }); }
YAML::Node toYaml(const Vector2& v) { return flowSequence({ v.x, v.y }); }
YAML::Node toYaml(const Quaternion& q) { return flowSequence({ q.x, q.y, q.z, q.w }); }

YAML::Node toYaml(const CFrame& cframe) {
    YAML::Node map(YAML::NodeType::Map);
    map["Position"] = toYaml(cframe.Position);
    map["Rotation"] = toYaml(cframe.Rotation);
    return map;
}

YAML::Node colorYaml(const RbxlxDocument::Color3& color, float transparency) {
    const float alpha = std::clamp(1.0f - transparency, 0.0f, 1.0f);
    return flowSequence({ color.r, color.g, color.b, alpha });
}

// =====================================================================
// Roblox 側の値の読み替え
// =====================================================================

// Enum.Material の値域で大まかに分類する（Recubin の MaterialType: Plastic=0, Wood=1, Metal=2, Stone=3）。
int materialTypeFromRoblox(int material) {
    if (material >= 512 && material < 784) return 1;    // Wood, WoodPlanks
    if (material >= 784 && material < 1024) return 3;   // Marble, Slate, Concrete, Granite, Brick, Rock ...
    if (material >= 1024 && material < 1280) return 2;  // CorrodedMetal, DiamondPlate, Foil, Metal
    return 0;
}

// Roblox NormalId (Right=0, Top=1, Back=2, Left=3, Bottom=4, Front=5) → Recubin Face
// (Front=0, Back=1, Top=2, Bottom=3, Right=4, Left=5)。どちらも Front は -Z。
int faceFromRoblox(int normalId) {
    static const int FACE_TABLE[6] = { 4, 2, 1, 5, 3, 0 };
    if (normalId < 0 || normalId >= 6) return 0;
    return FACE_TABLE[normalId];
}

RbxlxDocument::Color3 partColor(const RbxNode& rbx) {
    if (auto color = rbx.getColor("Color3uint8")) return *color;
    if (auto color = rbx.getColor("Color")) return *color;
    return RbxlxDocument::Color3{ 0.64f, 0.64f, 0.64f };
}

// rbxasset:// や rbxassetid:// などの Roblox 側アセットは取得できない。
bool isRobloxAsset(const std::string& url) {
    return url.rfind("rbxasset", 0) == 0 || url.rfind("http", 0) == 0;
}

std::string contentPath(const RbxNode& rbx, const char* key, Context& ctx) {
    const std::string url = rbx.getContent(key);
    if (url.empty()) return {};
    if (isRobloxAsset(url)) {
        addNote(ctx, rbx.className + "." + key + ": Roblox asset not imported");
        return {};
    }
    return url;
}

std::optional<CFrame> partCFrame(const RbxNode& rbx) { return rbx.getCFrame("CFrame"); }

// Model のワールド CFrame。WorldPivotData が無ければ子孫 Part の中心の範囲の中央にする。
CFrame modelWorldCFrame(const RbxNode& rbx) {
    if (auto pivot = rbx.getCFrame("WorldPivotData")) return *pivot;
    bool found = false;
    Vector3 minimum, maximum;
    auto visit = [&](auto& self, const RbxNode& node) -> void {
        for (const auto& child : node.children) {
            if (auto cframe = partCFrame(*child)) {
                const Vector3& p = cframe->Position;
                if (!found) { minimum = maximum = p; found = true; }
                minimum = Vector3(std::min(minimum.x, p.x), std::min(minimum.y, p.y), std::min(minimum.z, p.z));
                maximum = Vector3(std::max(maximum.x, p.x), std::max(maximum.y, p.y), std::max(maximum.z, p.z));
            }
            self(self, *child);
        }
    };
    visit(visit, rbx);
    if (!found) return CFrame();
    return CFrame((minimum + maximum) * 0.5f);
}

// Roblox 側のワールド CFrame（Attachment の親を求めるのに使う）。
CFrame rbxWorldCFrame(const RbxNode& rbx) {
    if (auto cframe = partCFrame(rbx)) return *cframe;
    if (rbx.className == "Model") return modelWorldCFrame(rbx);
    return CFrame();
}

// TimeOfDay ("14:30:00") → 時間
float hoursFromTimeOfDay(const std::string& timeOfDay) {
    int hours = 14, minutes = 0, seconds = 0;
    if (std::sscanf(timeOfDay.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) < 1) return 14.0f;
    return static_cast<float>(hours) + static_cast<float>(minutes) / 60.0f + static_cast<float>(seconds) / 3600.0f;
}

// 時刻と緯度から Sun.Angle (方角, 高度) をおおまかに求める。6 時に東から昇り、18 時に西へ沈む。
Vector2 sunAngleFromLighting(const RbxNode& lighting) {
    const float hours = hoursFromTimeOfDay(lighting.getString("TimeOfDay"));
    const float latitude = static_cast<float>(lighting.getNumber("GeographicLatitude", 0.0));
    const float dayPhase = (hours - 6.0f) / 12.0f * PI_F;
    const float elevation = std::asin(std::sin(dayPhase) * std::cos(latitude * PI_F / 180.0f)) * 180.0f / PI_F;
    float azimuth = std::fmod(90.0f + (hours - 6.0f) / 12.0f * 180.0f, 360.0f);
    if (azimuth < 0.0f) azimuth += 360.0f;
    return Vector2(azimuth, elevation);
}

// =====================================================================
// クラスごとの変換
// =====================================================================

using MapFunction = std::unique_ptr<OutNode> (*)(const RbxNode&, Context&);

std::unique_ptr<OutNode> mapPartAs(const RbxNode& rbx, Context&, const char* className) {
    auto out = makeNode(className, rbx);
    out->spatial = true;
    out->isCube = true;
    out->world = partCFrame(rbx).value_or(CFrame());

    Vector3 size = rbx.getVector3("size").value_or(rbx.getVector3("Size").value_or(Vector3(4, 1, 2)));
    if (std::string_view(className) == "Cylinder") {
        // Roblox の円柱は X 軸、Recubin は Y 軸。ローカル Y が元の X を向くように回す。
        out->world = out->world * CFrame(Vector3(0, 0, 0), Quaternion::fromAxisAngle(Vector3(0, 0, 1), -90.0f));
        size = Vector3(size.y, size.x, size.z);
    }

    YAML::Node& props = out->properties;
    props["Size"] = toYaml(size);
    props["Color"] = colorYaml(partColor(rbx), static_cast<float>(rbx.getNumber("Transparency", 0.0)));
    props["CastShadow"] = rbx.getBool("CastShadow", true);
    props["Anchored"] = rbx.getBool("Anchored", false);
    props["CanCollide"] = rbx.getBool("CanCollide", true);
    props["CanTouch"] = rbx.getBool("CanTouch", true);
    props["MaterialType"] = materialTypeFromRoblox(static_cast<int>(rbx.getNumber("Material", 256)));
    props["Locked"] = rbx.getBool("Locked", false);
    return out;
}

std::unique_ptr<OutNode> mapPart(const RbxNode& rbx, Context& ctx) {
    // PartType: Ball=0, Block=1, Cylinder=2, Wedge=3, CornerWedge=4
    switch (static_cast<int>(rbx.getNumber("shape", 1))) {
    case 0: return mapPartAs(rbx, ctx, "Sphere");
    case 2: return mapPartAs(rbx, ctx, "Cylinder");
    case 3: return mapPartAs(rbx, ctx, "Wedge");
    case 4:
        addNote(ctx, "Part(CornerWedge) converted to Cube");
        return mapPartAs(rbx, ctx, "Cube");
    default: return mapPartAs(rbx, ctx, "Cube");
    }
}

std::unique_ptr<OutNode> mapWedgePart(const RbxNode& rbx, Context& ctx) { return mapPartAs(rbx, ctx, "Wedge"); }
std::unique_ptr<OutNode> mapTrussPart(const RbxNode& rbx, Context& ctx) { return mapPartAs(rbx, ctx, "Truss"); }
std::unique_ptr<OutNode> mapSeat(const RbxNode& rbx, Context& ctx) { return mapPartAs(rbx, ctx, "Seat"); }

std::unique_ptr<OutNode> mapAsPlainCube(const RbxNode& rbx, Context& ctx) {
    addNote(ctx, rbx.className + " converted to Cube (shape not imported)");
    return mapPartAs(rbx, ctx, "Cube");
}

std::unique_ptr<OutNode> mapSpawnLocation(const RbxNode& rbx, Context& ctx) {
    auto out = mapPartAs(rbx, ctx, "SpawnLocation");
    out->properties["Enabled"] = rbx.getBool("Enabled", true);
    return out;
}

std::unique_ptr<OutNode> mapModel(const RbxNode& rbx, Context&) {
    auto out = makeNode("Model", rbx);
    out->spatial = true;
    out->world = modelWorldCFrame(rbx);
    const std::string primary = rbx.getRef("PrimaryPart");
    if (!primary.empty()) out->refs.push_back(PendingRef{ "PrimaryCube", primary });
    return out;
}

std::unique_ptr<OutNode> mapFolder(const RbxNode& rbx, Context&) { return makeNode("Folder", rbx); }

std::string partRef(const RbxNode& rbx, const char* key) {
    std::string referent = rbx.getRef(key);
    if (referent.empty()) referent = rbx.getRef(std::string(key) + "Internal");   // WeldConstraint
    return referent;
}

// Snap / Weld / WeldConstraint / ManualWeld。Recubin の Weld は現在の姿勢のまま溶接するので C0/C1 は使わない。
std::unique_ptr<OutNode> mapWeld(const RbxNode& rbx, Context&) {
    auto out = makeNode("Weld", rbx);
    out->properties["Enabled"] = rbx.getBool("Enabled", true);
    out->refs.push_back(PendingRef{ "Cube0", partRef(rbx, "Part0") });
    out->refs.push_back(PendingRef{ "Cube1", partRef(rbx, "Part1") });
    out->requireRefs = true;
    return out;
}

std::unique_ptr<OutNode> mapMotor6D(const RbxNode& rbx, Context&) {
    auto out = makeNode("Motor6D", rbx);
    out->properties["C0"] = toYaml(rbx.getCFrame("C0").value_or(CFrame()));
    out->properties["C1"] = toYaml(rbx.getCFrame("C1").value_or(CFrame()));
    out->refs.push_back(PendingRef{ "Part0", rbx.getRef("Part0") });
    out->refs.push_back(PendingRef{ "Part1", rbx.getRef("Part1") });
    out->requireRefs = true;
    return out;
}

// Roblox の Attachment.CFrame は親 Part からの相対値。
std::unique_ptr<OutNode> mapAttachment(const RbxNode& rbx, Context&) {
    auto out = makeNode("Attachment", rbx);
    out->spatial = true;
    const CFrame parentWorld = rbx.parent ? rbxWorldCFrame(*rbx.parent) : CFrame();
    out->world = parentWorld * rbx.getCFrame("CFrame").value_or(CFrame());
    return out;
}

std::unique_ptr<OutNode> mapSurfaceImage(const RbxNode& rbx, Context& ctx, const char* className) {
    auto out = makeNode(className, rbx);
    YAML::Node& props = out->properties;
    const std::string texture = contentPath(rbx, "Texture", ctx);
    if (!texture.empty()) props["Texture"] = texture;
    props["Face"] = faceFromRoblox(static_cast<int>(rbx.getNumber("Face", 5)));
    props["Color"] = colorYaml(rbx.getColor("Color3").value_or(RbxlxDocument::Color3{ 1, 1, 1 }),
                               static_cast<float>(rbx.getNumber("Transparency", 0.0)));
    return out;
}

std::unique_ptr<OutNode> mapDecal(const RbxNode& rbx, Context& ctx) { return mapSurfaceImage(rbx, ctx, "Decal"); }

std::unique_ptr<OutNode> mapTexture(const RbxNode& rbx, Context& ctx) {
    auto out = mapSurfaceImage(rbx, ctx, "Texture");
    out->properties["StudsPerTileU"] = static_cast<float>(rbx.getNumber("StudsPerTileU", 2.0));
    out->properties["StudsPerTileV"] = static_cast<float>(rbx.getNumber("StudsPerTileV", 2.0));
    return out;
}

std::unique_ptr<OutNode> mapHumanoid(const RbxNode& rbx, Context&) {
    auto out = makeNode("Humanoid", rbx);
    YAML::Node& props = out->properties;
    props["WalkSpeed"] = static_cast<float>(rbx.getNumber("WalkSpeed", 16.0));
    props["JumpPower"] = static_cast<float>(rbx.getNumber("JumpPower", 50.0));
    props["JumpHeight"] = static_cast<float>(rbx.getNumber("JumpHeight", 7.2));
    props["HipHeight"] = static_cast<float>(rbx.getNumber("HipHeight", 0.0));
    props["MaxHealth"] = static_cast<float>(rbx.getNumber("MaxHealth", 100.0));
    props["Health"] = static_cast<float>(rbx.getNumber("Health_XML", rbx.getNumber("MaxHealth", 100.0)));
    return out;
}

std::unique_ptr<OutNode> mapIntValue(const RbxNode& rbx, Context&) {
    auto out = makeNode("IntValue", rbx);
    out->properties["Value"] = static_cast<long long>(rbx.getNumber("Value", 0.0));
    return out;
}

std::unique_ptr<OutNode> mapNumberValue(const RbxNode& rbx, Context&) {
    auto out = makeNode("NumberValue", rbx);
    out->properties["Value"] = rbx.getNumber("Value", 0.0);
    return out;
}

std::unique_ptr<OutNode> mapBoolValue(const RbxNode& rbx, Context&) {
    auto out = makeNode("BoolValue", rbx);
    out->properties["Value"] = rbx.getBool("Value", false);
    return out;
}

std::unique_ptr<OutNode> mapVector3Value(const RbxNode& rbx, Context&) {
    auto out = makeNode("Vector3Value", rbx);
    out->properties["Value"] = toYaml(rbx.getVector3("Value").value_or(Vector3(0, 0, 0)));
    return out;
}

std::unique_ptr<OutNode> mapObjectValue(const RbxNode& rbx, Context&) {
    auto out = makeNode("ObjectValue", rbx);
    const std::string referent = rbx.getRef("Value");
    if (!referent.empty()) out->refs.push_back(PendingRef{ "Value", referent });
    return out;
}

std::string sanitizeFileName(const std::string& name) {
    std::string result;
    for (char c : name) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        result += safe ? c : '_';
    }
    return result.empty() ? std::string("Script") : result;
}

// Source を .luau に書き出して ContentPath に入れる（Recubin のシーンは Source を直接持たない）。
std::unique_ptr<OutNode> mapScriptAs(const RbxNode& rbx, Context& ctx, const char* className) {
    auto out = makeNode(className, rbx);
    if (std::string_view(className) != "ModuleScript") {
        out->properties["Enabled"] = !rbx.getBool("Disabled", false);
    }

    const fs::path file = ctx.scriptDir /
        (sanitizeFileName(out->name) + "_" + std::to_string(++ctx.scriptIndex) + ".luau");
    std::error_code ec;
    fs::create_directories(ctx.scriptDir, ec);
    std::string error;
    if (ec || !AtomicFile::writeReplacing(file, rbx.getString("Source"), error)) {
        RCBN_ERROR("[RbxlxConverter] Failed to write script " << file.string() << ": "
                   << (ec ? ec.message() : error));
        addNote(ctx, std::string(className) + ": failed to write source file");
        return out;
    }
    out->properties["ContentPath"] = fs::absolute(file, ec).string();
    return out;
}

std::unique_ptr<OutNode> mapScript(const RbxNode& rbx, Context& ctx) { return mapScriptAs(rbx, ctx, "Script"); }
std::unique_ptr<OutNode> mapLocalScript(const RbxNode& rbx, Context& ctx) { return mapScriptAs(rbx, ctx, "LocalScript"); }
std::unique_ptr<OutNode> mapModuleScript(const RbxNode& rbx, Context& ctx) { return mapScriptAs(rbx, ctx, "ModuleScript"); }

// 対応するものが無い、またはアセットを取得できないので子孫ごと捨てる。
std::unique_ptr<OutNode> dropNode(const RbxNode&, Context&) { return nullptr; }

const std::unordered_map<std::string, MapFunction>& classMappings() {
    static const std::unordered_map<std::string, MapFunction> table = {
        { "Part", mapPart },
        { "WedgePart", mapWedgePart },
        { "CornerWedgePart", mapAsPlainCube },
        { "TrussPart", mapTrussPart },
        { "Seat", mapSeat },
        { "VehicleSeat", mapSeat },
        { "SpawnLocation", mapSpawnLocation },
        { "MeshPart", mapAsPlainCube },
        { "UnionOperation", mapAsPlainCube },
        { "IntersectOperation", mapAsPlainCube },
        { "Model", mapModel },
        { "Folder", mapFolder },
        { "Snap", mapWeld },
        { "Weld", mapWeld },
        { "WeldConstraint", mapWeld },
        { "ManualWeld", mapWeld },
        { "Motor6D", mapMotor6D },
        { "Attachment", mapAttachment },
        { "Decal", mapDecal },
        { "Texture", mapTexture },
        { "Humanoid", mapHumanoid },
        { "IntValue", mapIntValue },
        { "NumberValue", mapNumberValue },
        { "BoolValue", mapBoolValue },
        { "Vector3Value", mapVector3Value },
        { "ObjectValue", mapObjectValue },
        { "Script", mapScript },
        { "LocalScript", mapLocalScript },
        { "ModuleScript", mapModuleScript },
        { "Camera", dropNode },
        { "Terrain", dropNode },
        { "NegateOperation", dropNode },
        { "SpecialMesh", dropNode },
        { "BodyColors", dropNode },
        { "Shirt", dropNode },
        { "Pants", dropNode },
        { "ShirtGraphic", dropNode },
        { "ScreenGui", dropNode },
        { "SurfaceGui", dropNode },
        { "BillboardGui", dropNode },
        { "Sky", dropNode },
        { "Atmosphere", dropNode },
        { "Clouds", dropNode },
        { "BloomEffect", dropNode },
        { "BlurEffect", dropNode },
        { "SunRaysEffect", dropNode },
        { "DepthOfFieldEffect", dropNode },
        { "ColorCorrectionEffect", dropNode },
    };
    return table;
}

// =====================================================================
// ツリーの変換
// =====================================================================

std::unique_ptr<OutNode> convertNode(const RbxNode& rbx, Context& ctx);

void convertChildren(const RbxNode& rbx, OutNode& out, Context& ctx) {
    for (const auto& child : rbx.children) {
        auto converted = convertNode(*child, ctx);
        if (!converted) continue;
        converted->parent = &out;
        out.children.push_back(std::move(converted));
    }
}

// 対応表に無いクラスは Folder にして子孫を変換し、何も残らなければ捨てる。
// 最上位（サービス）が空で捨てられるのは通常なので警告しない。
std::unique_ptr<OutNode> convertUnmapped(const RbxNode& rbx, Context& ctx) {
    auto folder = makeNode("Folder", rbx);
    convertChildren(rbx, *folder, ctx);
    if (folder->children.empty()) {
        if (rbx.parent) addSkipped(ctx, rbx.className);
        else ++ctx.skippedCount;
        return nullptr;
    }
    if (rbx.parent) addNote(ctx, rbx.className + " converted to Folder");
    ctx.byReferent[rbx.referent] = folder.get();
    return folder;
}

std::unique_ptr<OutNode> convertNode(const RbxNode& rbx, Context& ctx) {
    const auto& mappings = classMappings();
    const auto it = mappings.find(rbx.className);
    if (it == mappings.end()) return convertUnmapped(rbx, ctx);

    auto out = it->second(rbx, ctx);
    if (!out) {
        skipSubtree(rbx, ctx);
        return nullptr;
    }
    if (!rbx.referent.empty()) ctx.byReferent[rbx.referent] = out.get();
    convertChildren(rbx, *out, ctx);
    return out;
}

// Lighting は Recubin では Workspace 内の Lighting と Sun になる。Lighting の子（Sky や効果）は捨てる。
void appendLighting(OutNode& workspace, Context& ctx) {
    auto lighting = std::make_unique<OutNode>();
    lighting->className = "Lighting";
    lighting->name = "Lighting";
    lighting->parent = &workspace;
    workspace.children.insert(workspace.children.begin(), std::move(lighting));

    if (!ctx.lighting) return;
    auto sun = std::make_unique<OutNode>();
    sun->className = "Sun";
    sun->name = "Sun";
    sun->parent = &workspace;
    sun->properties["Angle"] = toYaml(sunAngleFromLighting(*ctx.lighting));
    workspace.children.insert(workspace.children.begin() + 1, std::move(sun));

    for (const auto& child : ctx.lighting->children) skipSubtree(*child, ctx);
}

std::unique_ptr<OutNode> convertWorkspace(const RbxNode& rbx, Context& ctx) {
    auto workspace = makeNode("Workspace", rbx);
    workspace->name = "Workspace";
    const float gravity = static_cast<float>(rbx.getNumber("Gravity", 196.2));
    workspace->properties["Gravity"] = toYaml(Vector3(0, -gravity, 0));
    convertChildren(rbx, *workspace, ctx);
    return workspace;
}

// Users / User は Recubin のシーンに必須の骨組み。
std::unique_ptr<OutNode> makeUsers() {
    auto users = std::make_unique<OutNode>();
    users->className = "Users";
    users->name = "Users";
    auto user = std::make_unique<OutNode>();
    user->className = "User";
    user->name = "User";
    user->parent = users.get();
    auto inventory = std::make_unique<OutNode>();
    inventory->className = "Folder";
    inventory->name = "Inventory";
    inventory->parent = user.get();
    user->children.push_back(std::move(inventory));
    users->children.push_back(std::move(user));
    return users;
}

void attachChild(OutNode& parent, std::unique_ptr<OutNode> child) {
    child->parent = &parent;
    parent.children.push_back(std::move(child));
}

std::unique_ptr<OutNode> buildScene(const RbxlxDocument::Document& document, Context& ctx) {
    auto root = std::make_unique<OutNode>();
    attachChild(*root, makeUsers());

    for (const auto& service : document.roots) {
        if (service->className == "Lighting") ctx.lighting = service.get();
    }

    bool hasWorkspace = false;
    for (const auto& service : document.roots) {
        if (service->className == "Workspace") {
            if (hasWorkspace) {
                addNote(ctx, "Extra Workspace ignored");
                skipSubtree(*service, ctx);
                continue;
            }
            auto workspace = convertWorkspace(*service, ctx);
            appendLighting(*workspace, ctx);
            attachChild(*root, std::move(workspace));
            hasWorkspace = true;
        } else if (service->className != "Lighting") {
            if (auto converted = convertNode(*service, ctx)) attachChild(*root, std::move(converted));
        }
    }
    if (!hasWorkspace) {
        auto workspace = std::make_unique<OutNode>();
        workspace->className = "Workspace";
        workspace->name = "Workspace";
        appendLighting(*workspace, ctx);
        attachChild(*root, std::move(workspace));
    }
    return root;
}

// =====================================================================
// 後処理: 名前の一意化 → 座標 → 参照
// =====================================================================

std::string sanitizeInstanceName(const std::string& name, const std::string& fallback) {
    std::string result = name;
    std::replace(result.begin(), result.end(), '\\', '_');
    return result.empty() ? fallback : result;
}

// Recubin は兄弟の名前が重なると自動でリネームし、パス参照が壊れるので先に一意にする。
void uniquifyNames(OutNode& node) {
    std::set<std::string> used;
    for (auto& child : node.children) {
        const std::string base = sanitizeInstanceName(child->name, child->className);
        std::string candidate = base;
        for (int index = 2; used.count(candidate); ++index) {
            candidate = base + "_" + std::to_string(index);
        }
        used.insert(candidate);
        child->name = candidate;
        uniquifyNames(*child);
    }
}

// Position/Rotation は最寄りの Spatial 祖先からのローカル値（spec: 座標系）。
void resolveTransforms(OutNode& node, const CFrame& coordinateParent) {
    CFrame childParent = coordinateParent;
    if (node.spatial) {
        node.local = coordinateParent.inverse() * node.world;
        childParent = node.world;
    }
    for (auto& child : node.children) resolveTransforms(*child, childParent);
}

// Workspace 内なら Workspace 相対、それ以外は最上位からのパス（SceneLoader の参照規約）。
std::string referencePath(const OutNode& target) {
    std::vector<const std::string*> names;
    for (const OutNode* node = &target; node && node->parent && node->className != "Workspace"; node = node->parent) {
        names.push_back(&node->name);
    }
    std::string path;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (!path.empty()) path += '\\';
        path += **it;
    }
    return path;
}

void resolveReferences(OutNode& node, Context& ctx) {
    for (const PendingRef& ref : node.refs) {
        const auto it = ref.referent.empty() ? ctx.byReferent.end() : ctx.byReferent.find(ref.referent);
        const OutNode* target = it == ctx.byReferent.end() ? nullptr : it->second;
        const bool needsCube = node.requireRefs || ref.key == "PrimaryCube";
        if (target && (!needsCube || target->isCube)) {
            node.properties[ref.key] = referencePath(*target);
        } else if (node.requireRefs) {
            node.removed = true;
            addNote(ctx, node.className + " removed: connected part not converted");
        }
    }
    for (auto& child : node.children) resolveReferences(*child, ctx);
}

// =====================================================================
// 書き出し
// =====================================================================

void emitNode(YAML::Emitter& out, const OutNode& node, int& count) {
    ++count;
    out << YAML::BeginMap;
    out << YAML::Key << "ClassName" << YAML::Value << node.className;
    out << YAML::Key << "Name" << YAML::Value << node.name;
    if (node.spatial || node.properties.size() > 0) {
        out << YAML::Key << "Properties" << YAML::Value << YAML::BeginMap;
        if (node.spatial) {
            out << YAML::Key << "Position" << YAML::Value << toYaml(node.local.Position);
            out << YAML::Key << "Rotation" << YAML::Value << toYaml(node.local.Rotation);
        }
        for (const auto& entry : node.properties) {
            out << YAML::Key << entry.first << YAML::Value << entry.second;
        }
        out << YAML::EndMap;
    }
    const bool hasChildren = std::any_of(node.children.begin(), node.children.end(),
        [](const auto& child) { return !child->removed; });
    if (hasChildren) {
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (const auto& child : node.children) {
            if (!child->removed) emitNode(out, *child, count);
        }
        out << YAML::EndSeq;
    }
    out << YAML::EndMap;
}

std::string emitScene(const OutNode& root, int& count) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "recubin" << YAML::Value << YAML::BeginMap
        << YAML::Key << "type" << YAML::Value << "scene"
        << YAML::Key << "version" << YAML::Value << 0
        << YAML::EndMap;
    out << YAML::Key << "Root" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "Properties" << YAML::Value << YAML::BeginMap
        << YAML::Key << "DefaultCameraMode" << YAML::Value << "Character"
        << YAML::EndMap;
    out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
    for (const auto& child : root.children) {
        if (!child->removed) emitNode(out, *child, count);
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

std::vector<std::string> summarizeWarnings(const Context& ctx) {
    std::vector<std::string> warnings;
    for (const auto& [className, count] : ctx.skippedClasses) {
        warnings.push_back("Skipped " + className + " x" + std::to_string(count));
    }
    for (const auto& [note, count] : ctx.notes) {
        warnings.push_back(note + " x" + std::to_string(count));
    }
    return warnings;
}

} // namespace

Result convertFile(const fs::path& input, const fs::path& output) {
    Result result;
    std::string error;
    auto document = RbxlxDocument::load(input, error);
    if (!document) {
        result.message = "Failed to read " + input.string() + ": " + error;
        RCBN_ERROR("[RbxlxConverter] " << result.message);
        return result;
    }

    Context ctx;
    ctx.scriptDir = output.parent_path() / (output.stem().string() + "_scripts");

    auto root = buildScene(*document, ctx);
    uniquifyNames(*root);
    resolveTransforms(*root, CFrame());
    resolveReferences(*root, ctx);

    int count = 0;
    const std::string yaml = emitScene(*root, count);
    if (!AtomicFile::writeReplacing(output, yaml, error)) {
        result.message = "Failed to write " + output.string() + ": " + error;
        RCBN_ERROR("[RbxlxConverter] " << result.message);
        return result;
    }

    result.success = true;
    result.convertedCount = count;
    result.skippedCount = ctx.skippedCount;
    result.warnings = summarizeWarnings(ctx);
    result.message = "Converted " + std::to_string(count) + " instances to " + output.string();
    return result;
}

} // namespace RbxlxConverter
