#include <Editor/PropertiesPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/UiHelpers.hpp>
#include <Editor/Localization.hpp>
#include <Core/Physics.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/User.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/FileRef.hpp>
#include <Instances/FontFile.hpp>
#include <Instances/Decal.hpp>
#include <Instances/SurfaceMark.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Canvas.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/LightSource.hpp>
#include <Instances/SpotLight.hpp>
#include <Instances/PostEffect.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
#include <random>
#include <Instances/Skybox.hpp>
#include <Instances/Rope.hpp>
#include <Instances/PhysicsConstraint.hpp>
#include <Instances/Rod.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Tool.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/TextLabel.hpp>
#include <Instances/TextButton.hpp>
#include <Instances/WorldGuiObject.hpp>
#include <Instances/SurfaceGui.hpp>
#include <Instances/BillboardGui.hpp>
#include <Instances/ProximityPrompt.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/NumberValue.hpp>
#include <Instances/CFrameValue.hpp>
#include <Instances/QuaternionValue.hpp>
#include <Instances/ObjectValue.hpp>
#include <Util/Color4.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Logger.hpp>
#include <Util/AssetPath.hpp>
#include <include/imgui/imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <system_error>

// ===================================================
//  PropertiesPanel 実装
// ===================================================

// ===================================================
//  スキーマ駆動インスペクタ
//  PropertyRegistry に登録済みのクラスは、ここが表を辿って自動でウィジェットを
//  描画し、編集を汎用 SetPropertyCommand として記録する（Undo / dirty 対応）。
//  → スキーマに1行足すだけでインスペクタに反映され、エディター取り残しを防ぐ。
// ===================================================
namespace {
class SetUserCursorCommand final : public Command {
public:
    enum class Kind { Type, Path, HotspotX, HotspotY, Size };
    SetUserCursorCommand(std::shared_ptr<User> target, Kind kind, std::size_t index,
                         int beforeInt, int afterInt, std::string beforePath, std::string afterPath)
        : m_target(std::move(target)), m_kind(kind), m_index(index),
          m_beforeInt(beforeInt), m_afterInt(afterInt),
          m_beforePath(std::move(beforePath)), m_afterPath(std::move(afterPath)) {}
    void execute() override { apply(m_afterInt, m_afterPath); }
    void undo() override { apply(m_beforeInt, m_beforePath); }
private:
    void apply(int value, const std::string& path) {
        if (!m_target) return;
        switch (m_kind) {
        case Kind::Type: m_target->setCursorType(static_cast<User::CursorType>(value)); break;
        case Kind::Path: m_target->setCursorImagePath(m_index, path); break;
        case Kind::HotspotX: m_target->setCursorHotspotX(m_index, value); break;
        case Kind::HotspotY: m_target->setCursorHotspotY(m_index, value); break;
        case Kind::Size: m_target->setCursorSize(m_index, value); break;
        }
    }
    std::shared_ptr<User> m_target;
    Kind m_kind;
    std::size_t m_index;
    int m_beforeInt, m_afterInt;
    std::string m_beforePath, m_afterPath;
};
}

// ローカライズ済みラベル + ImGui ID サフィックス（"##foo"）を連結するヘルパー
static std::string locId(Loc::LocKey key, const char* idSuffix) {
    return std::string(Loc::t(key)) + idSuffix;
}

// ファイル選択ダイアログはフルパスを返すため、プロジェクト(カレントディレクトリ)内の
// ファイルなら相対パスへ変換して格納する。プロジェクト外はそのまま絶対パスを返す。
// (シーンYAMLの可搬性とPackagerの相対パス同梱を助ける)
static std::string toProjectRelative(const std::string& absPath) {
    std::error_code ec;
    const std::filesystem::path absolute = AssetPath::fromStored(absPath);
    const std::filesystem::path project = std::filesystem::current_path(ec);
    if (ec) return AssetPath::toStored(absolute);
    const std::filesystem::path relative =
        std::filesystem::relative(absolute, project, ec);
    if (ec || relative.empty()) return AssetPath::toStored(absolute);
    const std::string stored = AssetPath::toStored(relative);
    if (stored == ".." || stored.rfind("../", 0) == 0)
        return AssetPath::toStored(absolute);
    return stored;
}

static void drawInstanceReferenceField(Instance* owner,
                                       const PropertyDesc& desc,
                                       CommandHistory* history,
                                       PickerState* picker) {
    if (!owner || desc.instanceRefClass.empty() || !desc.get || !desc.set) return;

    const std::string propertyName(desc.name);
    const std::string targetClass(desc.instanceRefClass);
    const std::string current = std::get<std::string>(desc.get(owner));
    const bool isPickingThis = picker && picker->active &&
        picker->constraint == owner && picker->prop == propertyName;
    const bool anyPicking = picker && picker->active;
    constexpr float BUTTON_WIDTH = 46.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    ImGui::PushID(&desc);
    ImGui::TextUnformatted(propertyName.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(
        std::max(1.0f, ImGui::GetContentRegionAvail().x - BUTTON_WIDTH * 2.0f - spacing * 2.0f));
    char reference[512] = {};
    std::snprintf(reference, sizeof(reference), "%s", current.c_str());
    ImGui::InputText("##instance_ref", reference, sizeof(reference),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();

    if (isPickingThis) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button("Cancel##pick", ImVec2(BUTTON_WIDTH, 0)))
            picker->active = false;
        ImGui::PopStyleColor();
    } else {
        if (anyPicking) ImGui::BeginDisabled();
        if (ImGui::Button("Pick##instance", ImVec2(BUTTON_WIDTH, 0)) && picker) {
            picker->active = true;
            picker->pickAttachment = false;
            picker->pickAnyInstance = false;
            picker->pickClassName = targetClass;
            picker->prop = propertyName;
            picker->constraint = owner;
            const std::weak_ptr<Instance> weakOwner = owner->shared_from_this();
            const PropertyDesc* property = &desc;
            picker->onPick = [weakOwner, property, targetClass, history](
                std::shared_ptr<Instance> picked) {
                auto target = weakOwner.lock();
                if (!target || !picked || !picked->IsA(targetClass)) return;
                const PropValue before = property->get(target.get());
                const PropValue after = PropValue(picked->getWorkspaceRelativePath());
                if (std::get<std::string>(before) == std::get<std::string>(after)) return;
                if (history) history->execute(std::make_unique<SetPropertyCommand>(
                    target, property, before, after));
                else PropertyRegistry::writeValue(target.get(), *property, after);
            };
        }
        if (anyPicking) ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (anyPicking || current.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Clear##instance", ImVec2(BUTTON_WIDTH, 0))) {
        const PropValue before = desc.get(owner);
        const PropValue after = PropValue(std::string{});
        if (history) history->execute(std::make_unique<SetPropertyCommand>(
            owner->shared_from_this(), &desc, before, after));
        else PropertyRegistry::writeValue(owner, desc, after);
    }
    if (anyPicking || current.empty()) ImGui::EndDisabled();
    ImGui::PopID();
}

static void drawFilePathField(Instance* owner, const PropertyDesc& desc,
                              CommandHistory* history) {
    if (!owner || desc.type != PropType::String || !desc.get || !desc.set) return;

    std::string dialogLabel(desc.editorDialogLabel);
    std::string dialogFilter(desc.editorDialogFilter);
    if ((dialogLabel.empty() || dialogFilter.empty()) && owner->IsA("PhysicalFileInstance")) {
        if (const auto* type = PhysicalFileInstanceRegistry::find(owner->getClassName())) {
            dialogLabel = std::string(type->dialogLabel);
            dialogFilter = std::string(type->dialogFilter);
        }
    }
    if (dialogLabel.empty()) dialogLabel = "Files (*.*)";
    if (dialogFilter.empty()) dialogFilter = "*.*";

    const std::string label(desc.name);
    const std::string current = std::get<std::string>(desc.get(owner));
    std::array<char, 4096> path{};
    std::snprintf(path.data(), path.size(), "%s", current.c_str());
    ImGui::PushID(&desc);
    // Keep the path editor on its own row.  The action buttons are placed
    // below it so a long path gets the full panel width instead of forcing
    // horizontal scrolling beside the buttons.
    ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x,
                                       120.0f, 640.0f));
    if (ImGui::InputText(label.c_str(), path.data(), path.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        const PropValue before = desc.get(owner);
        const PropValue after = PropValue(std::string(path.data()));
        if (std::get<std::string>(before) != std::get<std::string>(after)) {
            if (history) history->execute(std::make_unique<SetPropertyCommand>(
                owner->shared_from_this(), &desc, before, after));
            else PropertyRegistry::writeValue(owner, desc, after);
        }
    }
    if (ImGui::IsItemHovered() && !current.empty()) {
        ImGui::SetTooltip("%s", current.c_str());
    }
    ImGui::NewLine();
    if (ImGui::Button(Loc::t(Loc::LocKey::Browse))) {
        const std::string selected = getPlatform().openFileDialog({{dialogLabel, dialogFilter}});
        if (!selected.empty()) {
            const PropValue before = desc.get(owner);
            const PropValue after = PropValue(toProjectRelative(selected));
            if (std::get<std::string>(before) != std::get<std::string>(after)) {
                if (history) history->execute(std::make_unique<SetPropertyCommand>(
                    owner->shared_from_this(), &desc, before, after));
                else PropertyRegistry::writeValue(owner, desc, after);
            }
        }
    }
    ImGui::SameLine();
    if (current.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Clear")) {
        const PropValue before = desc.get(owner);
        const PropValue after = PropValue(std::string{});
        if (history) history->execute(std::make_unique<SetPropertyCommand>(
            owner->shared_from_this(), &desc, before, after));
        else PropertyRegistry::writeValue(owner, desc, after);
    }
    if (current.empty()) ImGui::EndDisabled();
    ImGui::PopID();
}

static void renderSchemaInspector(Instance* inst, const char* className,
                                  CommandHistory* history, PickerState* picker) {
    (void)className;
    static PropValue s_before;  // 編集開始時の値（同時編集は1つなので単一でよい）
    // The runtime type is the source of truth.  The old className-only lookup
    // silently omitted derived properties (notably Motor6D/Gyro and the
    // concrete two-body constraints) from the inspector.
    for (const PropertyDesc* dp : PropertyRegistry::collectApplicableSchema(inst)) {
        if (!dp) continue;
        const PropertyDesc& d = *dp;
        if (d.kind != PropKind::Field || !d.editable || !d.get) continue;
        if (d.editorWidget == EditorWidget::FilePath) {
            drawFilePathField(inst, d, history);
            continue;
        }
        if (!d.instanceRefClass.empty()) {
            drawInstanceReferenceField(inst, d, history, picker);
            continue;
        }
        const bool readOnly = d.editorReadOnly || !d.set;
        std::string label(d.name);
        // liveSet があればドラッグ中はそちらを使う（軽量反映）。無ければ set をそのまま使う
        auto applyLive = [&d](Instance* o, const PropValue& v) {
            if (d.liveSet) d.liveSet(o, v); else d.set(o, v);
        };
        if (!d.separator.empty()) ImGui::SeparatorText(d.separator.data());
        ImGui::PushID(static_cast<int>(reinterpret_cast<std::uintptr_t>(dp)));
        if (readOnly) ImGui::BeginDisabled();

        PropValue cur = d.get(inst);
        bool itemActivated = false;
        bool itemDeactivatedAfterEdit = false;
        switch (d.type) {
            case PropType::Float: {
                float v = std::get<float>(cur);
                if (ImGui::DragFloat(label.c_str(), &v, d.step, d.lo, d.hi, "%.2f")) applyLive(inst, PropValue(v));
                break;
            }
            case PropType::Int: {
                int v = std::get<int>(cur);
                if (ImGui::DragInt(label.c_str(), &v, 1.0f, (int)d.lo, (int)d.hi)) applyLive(inst, PropValue(v));
                break;
            }
            case PropType::Bool: {
                bool v = std::get<bool>(cur);
                if (ImGui::Checkbox(label.c_str(), &v)) applyLive(inst, PropValue(v));
                break;
            }
            case PropType::String: {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s", std::get<std::string>(cur).c_str());
                if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) applyLive(inst, PropValue(std::string(buf)));
                break;
            }
            case PropType::Vec3: {
                Vector3 v = std::get<Vector3>(cur);
                if (ImGui::DragFloat3(label.c_str(), &v.x, d.step, d.lo, d.hi, "%.2f")) applyLive(inst, PropValue(v));
                break;
            }
            case PropType::Vec2: {
                Vector2 v = std::get<Vector2>(cur);
                if (ImGui::DragFloat2(label.c_str(), &v.x, d.step, d.lo, d.hi, "%.2f")) applyLive(inst, PropValue(v));
                break;
            }
            case PropType::Color4: {
                Color4 v = std::get<Color4>(cur);
                if (ImGui::ColorEdit4(label.c_str(), &v.r)) applyLive(inst, PropValue(v));
                break;
            }
            case PropType::CFrame: {
                CFrame v = std::get<CFrame>(cur);
                ImGui::TextUnformatted(label.c_str());
                ImGui::Indent();
                if (ImGui::DragFloat3("Position", &v.Position.x, d.step, d.lo, d.hi, "%.2f"))
                    applyLive(inst, PropValue(v));
                itemActivated = ImGui::IsItemActivated();
                itemDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                Vector3 rotation = v.Rotation.toEuler();
                if (ImGui::DragFloat3("Rotation", &rotation.x, 1.0f, -360.0f, 360.0f, "%.1f")) {
                    v.Rotation = Quaternion::fromEuler(rotation);
                    applyLive(inst, PropValue(v));
                }
                itemActivated = itemActivated || ImGui::IsItemActivated();
                itemDeactivatedAfterEdit = itemDeactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit();
                ImGui::Unindent();
                break;
            }
            case PropType::Quaternion: {
                Quaternion v = std::get<Quaternion>(cur);
                Vector3 rotation = v.toEuler();
                if (ImGui::DragFloat3(label.c_str(), &rotation.x, 1.0f, -360.0f, 360.0f, "%.1f"))
                    applyLive(inst, PropValue(Quaternion::fromEuler(rotation)));
                break;
            }
            case PropType::Enum: {
                int iv = std::get<int>(cur);
                int idx = 0;
                std::vector<const char*> items;
                for (size_t i = 0; i < d.enumNames.size(); ++i) {
                    items.push_back(d.enumNames[i].first.data());
                    if (d.enumNames[i].second == iv) idx = (int)i;
                }
                if (ImGui::Combo(label.c_str(), &idx, items.data(), (int)items.size()))
                    applyLive(inst, PropValue(d.enumNames[idx].second));
                break;
            }
        }

        // 編集の開始/確定を捉えて Undo 1ステップとして記録する
        if (itemActivated || ImGui::IsItemActivated())
            s_before = PropertyRegistry::readValue(inst, d);
        if ((itemDeactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit()) && history) {
            PropValue after = PropertyRegistry::readValue(inst, d);
            if (d.liveSet) d.set(inst, after);  // liveSet運用のプロパティのみ、確定時に本来の set（actor再生成等）を適用
            history->record(std::make_unique<SetPropertyCommand>(
                inst->shared_from_this(), dp, s_before, after));
        }
        if (readOnly) ImGui::EndDisabled();
        ImGui::PopID();
    }
}

// ===================================================
//  複数選択時の共通プロパティ一括編集インスペクタ
//  全選択インスタンスに共通するスキーマ駆動プロパティ（Field/editable/get&&set）
//  だけを積集合として取り出し、値が食い違う（mixed）場合は各ウィジェットで
//  それを示しつつ、編集は選択中の全インスタンスへ同時適用する。
//  Undo は CompositeCommand に SetPropertyCommand を束ねて1操作として記録する。
// ===================================================
static bool parseFloats(const char* text, float* out, int count) {
    if (!text || !out || count <= 0) return false;
    const char* p=text;
    for (int i=0;i<count;++i) {
        char* end=nullptr; out[i]=std::strtof(p,&end);
        if (end==p || !std::isfinite(out[i])) return false;
        p=end; while (*p==' '||*p=='\t'||*p==',') ++p;
    }
    while (*p==' '||*p=='\t'||*p==',') ++p;
    if (*p!='\0') return false;
    return true;
}

static bool propValuesEqual(const PropValue& left, const PropValue& right) {
    if (left.index() != right.index()) return false;
    switch (left.index()) {
        case 0: return std::get<float>(left) == std::get<float>(right);
        case 1: return std::get<int>(left) == std::get<int>(right);
        case 2: return std::get<bool>(left) == std::get<bool>(right);
        case 3: return std::get<std::string>(left) == std::get<std::string>(right);
        case 4: return std::get<Vector3>(left) == std::get<Vector3>(right);
        case 5: return std::get<Vector2>(left) == std::get<Vector2>(right);
        case 6: return std::get<Color4>(left) == std::get<Color4>(right);
        case 7: {
            const CFrame& a = std::get<CFrame>(left);
            const CFrame& b = std::get<CFrame>(right);
            return a.Position == b.Position && a.Rotation.w == b.Rotation.w &&
                   a.Rotation.x == b.Rotation.x && a.Rotation.y == b.Rotation.y &&
                   a.Rotation.z == b.Rotation.z;
        }
        case 8: {
            const Quaternion& a = std::get<Quaternion>(left);
            const Quaternion& b = std::get<Quaternion>(right);
            return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
        }
        default: return false;
    }
}

// Multi-edit is descriptor-driven in renderMultiInspector below.

static void renderMultiInspector(const std::vector<Instance*>& sel, CommandHistory* history,
                                 PickerState* picker) {
    static std::vector<PropValue> s_multiBefore;

    std::vector<Instance*> valid;
    for (Instance* inst : sel) {
        if (!inst) continue;
        if (inst->Parent.expired() && !inst->IsA("System")) continue;
        valid.push_back(inst);
    }

    ImGui::Text(Loc::t(Loc::LocKey::MultiSelectedCount), (int)valid.size());
    if (valid.size() < 2) return;

    // Nameは選択順に base, base1, ... を割り当てる一括編集。
    static char multiName[256] = {};
    const bool multiNameSubmit = ImGui::InputTextWithHint(
        "Name", "base, base1, ...", multiName, sizeof(multiName), ImGuiInputTextFlags_EnterReturnsTrue);
    if(ImGui::IsItemActivated()) multiName[0]='\0';
    if((multiNameSubmit || ImGui::IsItemDeactivatedAfterEdit()) && multiName[0]!='\0') {
        std::vector<MultiRenameInstanceCommand::Entry> entries;
        std::unordered_set<Instance*> selected;
        for (Instance* i : valid) if (!i->isRuntimeNameLocked()) selected.insert(i);
        std::unordered_map<Instance*,std::unordered_set<std::string>> occupied;
        for(Instance* i:valid){auto p=i->Parent.lock();if(!p)continue;auto& set=occupied[p.get()];for(auto& [n,ch]:p->getChildren())if(!selected.count(ch.get()))set.insert(n);}
        size_t suffix=0;
        for(Instance* t:valid){if(t->isRuntimeNameLocked())continue;auto p=t->Parent.lock();std::string desired=std::string(multiName)+(suffix?std::to_string(suffix):"");if(p){auto& used=occupied[p.get()];while(used.count(desired)){++suffix;desired=std::string(multiName)+std::to_string(suffix);}used.insert(desired);}++suffix;if(desired!=t->Name)entries.push_back({t->shared_from_this(),t->Name,desired});}
        if(!entries.empty()){auto command=std::make_unique<MultiRenameInstanceCommand>(std::move(entries));command->execute();if(history)history->record(std::move(command));}
    }
    // multiEditKey（未指定時は表示名）と、値型・Editor種別・参照対象型が
    // 一致するプロパティだけを同じ一括編集行として扱う。
    auto multiCompatibilityKey = [](const PropertyDesc& desc) {
        const std::string_view editKey = desc.multiEditKey.empty() ? desc.name : desc.multiEditKey;
        std::string key;
        const auto appendPart = [&key](std::string_view value) {
            key += std::to_string(value.size());
            key += ':';
            key.append(value);
            key += ';';
        };
        appendPart(editKey);
        key += std::to_string(static_cast<int>(desc.type));
        key += ';';
        key += std::to_string(static_cast<int>(desc.editorWidget));
        key += ';';
        appendPart(desc.instanceRefClass);
        return key;
    };

    // 実行時型に適用されるスキーマ集合。具象型が未登録でも、IsA() に
    // 一致する基底（PhysicsConstraint など）の共通プロパティを含める。
    auto buildSchema = [&multiCompatibilityKey](Instance* inst) {
        std::vector<const PropertyDesc*> result;
        std::unordered_set<std::string> seen;
        for (const auto* d : PropertyRegistry::collectApplicableSchema(inst)) {
            if (!seen.insert(multiCompatibilityKey(*d)).second) continue;
            result.push_back(d);
        }
        return result;
    };

    struct MultiPropertyRow {
        const PropertyDesc* display = nullptr;
        std::vector<std::pair<Instance*, const PropertyDesc*>> targets;
    };
    std::vector<MultiPropertyRow> propertyRows;
    std::unordered_map<std::string, size_t> rowIndex;
    for (Instance* inst : valid) {
        std::unordered_set<std::string> seenForInstance;
        for (const PropertyDesc* desc : buildSchema(inst)) {
            if (desc->kind != PropKind::Field || !desc->multiEditable ||
                desc->editorReadOnly ||
                !desc->get || !desc->set)
                continue;
            if (desc->name == "Name")
                continue;
            const std::string key = multiCompatibilityKey(*desc);
            if (!seenForInstance.insert(key).second) continue;
            auto [it, inserted] = rowIndex.emplace(key, propertyRows.size());
            if (inserted) propertyRows.push_back({desc, {}});
            propertyRows[it->second].targets.emplace_back(inst, desc);
        }
    }

    // 和集合を走査し、対応する型だけへ変更を適用する。
    for (auto& propertyRow : propertyRows) {
        const PropertyDesc* d0 = propertyRow.display;
        auto& rows = propertyRow.targets;
        std::string name(d0->name);
        if (rows.size() != valid.size())
            name += " (" + std::to_string(rows.size()) + "/" + std::to_string(valid.size()) + ")";

        if (!d0->separator.empty()) ImGui::SeparatorText(d0->separator.data());
        ImGui::PushID(multiCompatibilityKey(*d0).c_str());

        // liveSet があればドラッグ中はそちらを使う（軽量反映）。無ければ set をそのまま使う
        auto applyLiveAll = [&rows](const PropValue& v) {
            for (auto& [inst, d] : rows) {
                if (d->liveSet) d->liveSet(inst, v); else d->set(inst, v);
            }
        };

        // 全員の現在値を読み、一致しているかどうかを判定する（mixed 表示用）
        std::vector<PropValue> curVals;
        curVals.reserve(rows.size());
        for (auto& [inst, d] : rows) curVals.push_back(d->get(inst));
        bool mixed = false;
        for (size_t i = 1; i < curVals.size(); ++i) {
            if (!propValuesEqual(curVals[i], curVals[0])) { mixed = true; break; }
        }
        PropValue cur = curVals[0];
        bool recordedImmediately = false;
        bool itemActivated = false;
        bool itemDeactivatedAfterEdit = false;
        auto recordCurrentValues = [&]() {
            if (!history) return;
            auto composite = std::make_unique<CompositeCommand>();
            for (size_t i = 0; i < rows.size(); ++i) {
                Instance* target = rows[i].first;
                const PropertyDesc* desc = rows[i].second;
                const PropValue after = desc->get(target);
                if (!propValuesEqual(curVals[i], after)) {
                    composite->add(std::make_unique<SetPropertyCommand>(
                        target->shared_from_this(), desc, curVals[i], after));
                }
            }
            if (!composite->empty()) history->record(std::move(composite));
        };

        if (d0->editorWidget == EditorWidget::FilePath) {
            const std::string current = mixed
                ? std::string(Loc::t(Loc::LocKey::MixedValue))
                : std::get<std::string>(cur);
            ImGui::Text("%s: %s", name.c_str(), current.empty() ? "(none)" : current.c_str());
            std::string dialogLabel(d0->editorDialogLabel);
            std::string dialogFilter(d0->editorDialogFilter);
            if ((dialogLabel.empty() || dialogFilter.empty()) &&
                !rows.empty() && rows.front().first->IsA("PhysicalFileInstance")) {
                if (const auto* type = PhysicalFileInstanceRegistry::find(
                        rows.front().first->getClassName())) {
                    dialogLabel = std::string(type->dialogLabel);
                    dialogFilter = std::string(type->dialogFilter);
                }
            }
            if (dialogLabel.empty()) dialogLabel = "Files (*.*)";
            if (dialogFilter.empty()) dialogFilter = "*.*";
            if (ImGui::Button((std::string(Loc::t(Loc::LocKey::Browse)) + "##multi_path").c_str())) {
                const std::string selected = getPlatform().openFileDialog({
                    {dialogLabel, dialogFilter}});
                if (!selected.empty()) {
                    applyLiveAll(PropValue(toProjectRelative(selected)));
                    recordCurrentValues();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear##multi_path")) {
                applyLiveAll(PropValue(std::string{}));
                recordCurrentValues();
            }
            ImGui::PopID();
            continue;
        }

        if (d0->editorWidget == EditorWidget::InstanceReference) {
            const std::string current = mixed
                ? std::string(Loc::t(Loc::LocKey::MixedValue))
                : std::get<std::string>(cur);
            ImGui::Text("%s: %s", name.c_str(), current.empty() ? "(none)" : current.c_str());
            if (picker && ImGui::Button("Pick##multi_reference")) {
                using Target = std::pair<std::weak_ptr<Instance>, const PropertyDesc*>;
                std::vector<Target> targets;
                targets.reserve(rows.size());
                for (const auto& [target, desc] : rows)
                    targets.emplace_back(target->shared_from_this(), desc);
                const std::string targetClass(d0->instanceRefClass);
                picker->active = true;
                picker->pickAnyInstance = false;
                picker->pickAttachment = false;
                picker->pickClassName = targetClass;
                picker->prop.clear();
                picker->constraint = nullptr;
                picker->onPick = [targets = std::move(targets), targetClass, history](
                                      std::shared_ptr<Instance> picked) mutable {
                    if (!picked || !picked->IsA(targetClass)) return;
                    auto composite = std::make_unique<CompositeCommand>();
                    for (const auto& [weakTarget, desc] : targets) {
                        auto target = weakTarget.lock();
                        if (!target || !desc || !desc->get || !desc->set) continue;
                        const PropValue before = desc->get(target.get());
                        const PropValue after = PropValue(picked->getWorkspaceRelativePath());
                        if (!propValuesEqual(before, after))
                            composite->add(std::make_unique<SetPropertyCommand>(
                                target, desc, before, after));
                    }
                    if (composite->empty()) return;
                    if (history) history->execute(std::move(composite));
                    else composite->execute();
                };
            }
            ImGui::SameLine();
            const bool canClear = !mixed && !std::get<std::string>(cur).empty();
            if (!canClear) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##multi_reference")) {
                applyLiveAll(PropValue(std::string{}));
                recordCurrentValues();
            }
            if (!canClear) ImGui::EndDisabled();
            ImGui::PopID();
            continue;
        }

        switch (d0->type) {
            case PropType::Float: {
                float v = std::get<float>(cur);
                const char* fmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%.2f";
                if (ImGui::DragFloat(name.c_str(), &v, d0->step, d0->lo, d0->hi, fmt)) applyLiveAll(PropValue(v));
                break;
            }
            case PropType::Int: {
                int v = std::get<int>(cur);
                const char* fmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%d";
                if (ImGui::DragInt(name.c_str(), &v, 1.0f, (int)d0->lo, (int)d0->hi, fmt)) applyLiveAll(PropValue(v));
                break;
            }
            case PropType::Bool: {
                bool v = std::get<bool>(cur);
                bool changed = ImGui::Checkbox(name.c_str(), &v);
                if (mixed) { ImGui::SameLine(); ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::MixedValue)); }
                if (changed) {
                    applyLiveAll(PropValue(v));
                    recordCurrentValues();
                    recordedImmediately = true;
                }
                break;
            }
            case PropType::String: {
                char buf[256];
                if (mixed) {
                    buf[0] = '\0';
                    if (ImGui::InputTextWithHint(name.c_str(), Loc::t(Loc::LocKey::MixedValue), buf, sizeof(buf)))
                        applyLiveAll(PropValue(std::string(buf)));
                } else {
                    std::snprintf(buf, sizeof(buf), "%s", std::get<std::string>(cur).c_str());
                    if (ImGui::InputText(name.c_str(), buf, sizeof(buf)))
                        applyLiveAll(PropValue(std::string(buf)));
                }
                break;
            }
            case PropType::Vec3: {
                Vector3 v = std::get<Vector3>(cur);
                const char* fmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%.2f";
                if (ImGui::DragFloat3(name.c_str(), &v.x, d0->step, d0->lo, d0->hi, fmt)) applyLiveAll(PropValue(v));
                break;
            }
            case PropType::Vec2: {
                Vector2 v = std::get<Vector2>(cur);
                const char* fmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%.2f";
                if (ImGui::DragFloat2(name.c_str(), &v.x, d0->step, d0->lo, d0->hi, fmt)) applyLiveAll(PropValue(v));
                break;
            }
            case PropType::Color4: {
                Color4 v = std::get<Color4>(cur);
                bool changed = ImGui::ColorEdit4(name.c_str(), &v.r);
                if (mixed) { ImGui::SameLine(); ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::MixedValue)); }
                if (changed) applyLiveAll(PropValue(v));
                break;
            }
            case PropType::CFrame: {
                CFrame v = std::get<CFrame>(cur);
                ImGui::TextUnformatted(name.c_str());
                ImGui::Indent();
                const char* fmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%.2f";
                if (ImGui::DragFloat3("Position", &v.Position.x, d0->step, d0->lo, d0->hi, fmt))
                    applyLiveAll(PropValue(v));
                itemActivated = ImGui::IsItemActivated();
                itemDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
                Vector3 rotation = v.Rotation.toEuler();
                const char* rotationFmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%.1f";
                if (ImGui::DragFloat3("Rotation", &rotation.x, 1.0f, -360.0f, 360.0f, rotationFmt)) {
                    v.Rotation = Quaternion::fromEuler(rotation);
                    applyLiveAll(PropValue(v));
                }
                itemActivated = itemActivated || ImGui::IsItemActivated();
                itemDeactivatedAfterEdit = itemDeactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit();
                ImGui::Unindent();
                break;
            }
            case PropType::Quaternion: {
                Quaternion v = std::get<Quaternion>(cur);
                Vector3 rotation = v.toEuler();
                const char* fmt = mixed ? Loc::t(Loc::LocKey::MixedValue) : "%.1f";
                if (ImGui::DragFloat3(name.c_str(), &rotation.x, 1.0f, -360.0f, 360.0f, fmt))
                    applyLiveAll(PropValue(Quaternion::fromEuler(rotation)));
                break;
            }
            case PropType::Enum: {
                if (mixed) {
                    if (ImGui::BeginCombo(name.c_str(), Loc::t(Loc::LocKey::MixedValue))) {
                        for (const auto& enumEntry : d0->enumNames) {
                            std::string enumLabel(enumEntry.first);
                            if (ImGui::Selectable(enumLabel.c_str())) {
                                // Selectable は Combo 本体を「編集済み」にしないため、下の
                                // IsItemDeactivatedAfterEdit では拾えない。この場で Undo を記録する
                                applyLiveAll(PropValue(enumEntry.second));
                                if (history) {
                                    auto composite = std::make_unique<CompositeCommand>();
                                    for (size_t i = 0; i < rows.size(); ++i) {
                                        Instance* rInst = rows[i].first;
                                        const PropertyDesc* rd = rows[i].second;
                                        composite->add(std::make_unique<SetPropertyCommand>(
                                            rInst->shared_from_this(), rd, curVals[i], rd->get(rInst)));
                                    }
                                    if (!composite->empty()) history->record(std::move(composite));
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    int iv = std::get<int>(cur);
                    int idx = 0;
                    std::vector<const char*> items;
                    for (size_t i = 0; i < d0->enumNames.size(); ++i) {
                        items.push_back(d0->enumNames[i].first.data());
                        if (d0->enumNames[i].second == iv) idx = (int)i;
                    }
                    if (ImGui::Combo(name.c_str(), &idx, items.data(), (int)items.size()))
                        applyLiveAll(PropValue(d0->enumNames[idx].second));
                }
                break;
            }
        }

        // 編集の開始/確定を捉えて、全員分の SetPropertyCommand を1つの CompositeCommand として記録する
        if (itemActivated || ImGui::IsItemActivated()) {
            s_multiBefore.clear();
            for (auto& [inst, d] : rows) s_multiBefore.push_back(d->get(inst));
        }
        if (!recordedImmediately && (itemDeactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit()) && history) {
            auto composite = std::make_unique<CompositeCommand>();
            for (size_t i = 0; i < rows.size(); ++i) {
                Instance* inst = rows[i].first;
                const PropertyDesc* d = rows[i].second;
                PropValue after = d->get(inst);
                if (d->liveSet) d->set(inst, after);  // liveSet運用のプロパティのみ、確定時に本来の set（actor再生成等）を適用
                PropValue before = (i < s_multiBefore.size()) ? s_multiBefore[i] : after;
                composite->add(std::make_unique<SetPropertyCommand>(
                    inst->shared_from_this(), d, before, after));
            }
            if (!composite->empty())
                history->record(std::move(composite));
        }

        ImGui::PopID();
    }
}

namespace {

enum class HumanoidAnimationSlot { Walk, Jump, Equip };

static std::shared_ptr<Animation> animationForSlot(const Humanoid& humanoid,
                                                    HumanoidAnimationSlot slot) {
    switch (slot) {
        case HumanoidAnimationSlot::Walk: return humanoid.getWalkAnimation();
        case HumanoidAnimationSlot::Jump: return humanoid.getJumpAnimation();
        case HumanoidAnimationSlot::Equip: return humanoid.getEquipAnimation();
    }
    return nullptr;
}

static void setAnimationForSlot(Humanoid& humanoid, HumanoidAnimationSlot slot,
                                const std::shared_ptr<Animation>& animation) {
    switch (slot) {
        case HumanoidAnimationSlot::Walk: humanoid.setWalkAnimation(animation); break;
        case HumanoidAnimationSlot::Jump: humanoid.setJumpAnimation(animation); break;
        case HumanoidAnimationSlot::Equip: humanoid.setEquipAnimation(animation); break;
    }
}

static const std::string& animationPathForSlot(const Humanoid& humanoid,
                                                HumanoidAnimationSlot slot) {
    switch (slot) {
        case HumanoidAnimationSlot::Walk: return humanoid.getWalkAnimationPath();
        case HumanoidAnimationSlot::Jump: return humanoid.getJumpAnimationPath();
        case HumanoidAnimationSlot::Equip: return humanoid.getEquipAnimationPath();
    }
    return humanoid.getWalkAnimationPath();
}

static void setAnimationPathForSlot(Humanoid& humanoid, HumanoidAnimationSlot slot,
                                    const std::string& path) {
    switch (slot) {
        case HumanoidAnimationSlot::Walk: humanoid.setWalkAnimationPath(path); break;
        case HumanoidAnimationSlot::Jump: humanoid.setJumpAnimationPath(path); break;
        case HumanoidAnimationSlot::Equip: humanoid.setEquipAnimationPath(path); break;
    }
}

struct HumanoidAnimationReferenceState {
    std::shared_ptr<Animation> animation;
    std::string unresolvedPath;
};

struct SetHumanoidAnimationCommand final : Command {
    std::shared_ptr<Humanoid> humanoid;
    HumanoidAnimationSlot slot;
    HumanoidAnimationReferenceState before;
    HumanoidAnimationReferenceState after;

    SetHumanoidAnimationCommand(std::shared_ptr<Humanoid> humanoidValue,
                                HumanoidAnimationSlot slotValue,
                                HumanoidAnimationReferenceState beforeValue,
                                HumanoidAnimationReferenceState afterValue)
        : humanoid(std::move(humanoidValue)), slot(slotValue),
          before(std::move(beforeValue)), after(std::move(afterValue)) {}

    void execute() override {
        apply(after);
    }
    void undo() override {
        apply(before);
    }
private:
    void apply(const HumanoidAnimationReferenceState& state) {
        if (!humanoid) return;
        if (state.animation) setAnimationForSlot(*humanoid, slot, state.animation);
        else setAnimationPathForSlot(*humanoid, slot, state.unresolvedPath);
    }
};

static void collectAnimations(Instance* node, std::vector<std::shared_ptr<Animation>>& output) {
    if (!node) return;
    if (node->IsA("Animation"))
        output.push_back(std::static_pointer_cast<Animation>(node->shared_from_this()));
    for (const auto& [name, child] : node->getChildren()) {
        (void)name;
        collectAnimations(child.get(), output);
    }
}

static const PropertyDesc* screenGuiFontProperty() {
    for (const auto& desc : PropertyRegistry::schemaFor("ScreenGuiObject")) {
        if (desc.name == "Font") return &desc;
    }
    return nullptr;
}

static void drawSystemFontSelection(ScreenGuiObject* gui, CommandHistory* history) {
    if (!gui) return;
    const PropertyDesc* desc = screenGuiFontProperty();
    if (!desc) return;

    int current = static_cast<int>(gui->Font);
    const char* preview = "Default";
    for (const auto& [name, value] : desc->enumNames)
        if (value == current) preview = name.data();

    if (!ImGui::BeginCombo("Font", preview)) return;
    const auto target = gui->shared_from_this();
    for (const auto& [name, value] : desc->enumNames) {
        const bool selected = value == current;
        if (ImGui::Selectable(name.data(), selected) && !selected) {
            const PropValue before = desc->get(gui);
            const PropValue after = PropValue(value);
            if (history) history->execute(std::make_unique<SetPropertyCommand>(
                target, desc, before, after));
            else PropertyRegistry::writeValue(gui, *desc, after);
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

static void drawFontSelection(ScreenGuiObject* gui,
                              CommandHistory* history,
                              PickerState* picker) {
    if (!gui) return;
    if (gui->UseFontFile) {
        for (const auto& desc : PropertyRegistry::schemaFor("ScreenGuiObject")) {
            if (desc.name == "FontFile") {
                drawInstanceReferenceField(gui, desc, history, picker);
                break;
            }
        }
    } else drawSystemFontSelection(gui, history);
}

static void drawHumanoidAnimationReference(const char* label,
                                            const std::shared_ptr<Humanoid>& humanoid,
                                            HumanoidAnimationSlot slot,
    CommandHistory* history) {
    auto current = animationForSlot(*humanoid, slot);
    const std::string storedPath = animationPathForSlot(*humanoid, slot);
    const HumanoidAnimationReferenceState currentState{current, storedPath};
    const std::string preview = current ? current->getFullPath()
        : (storedPath.empty() ? "(None)" : "Unresolved: " + storedPath);
    if (!ImGui::BeginCombo(label, preview.c_str())) return;

    if (ImGui::Selectable("(None)", !current && storedPath.empty()) &&
        (current || !storedPath.empty())) {
        if (history) history->execute(std::make_unique<SetHumanoidAnimationCommand>(
            humanoid, slot, currentState, HumanoidAnimationReferenceState{}));
        else setAnimationForSlot(*humanoid, slot, nullptr);
    }

    Instance* root = humanoid.get();
    while (auto parent = root->Parent.lock()) root = parent.get();
    std::vector<std::shared_ptr<Animation>> animations;
    collectAnimations(root, animations);
    std::sort(animations.begin(), animations.end(),
              [](const auto& a, const auto& b) { return a->getFullPath() < b->getFullPath(); });
    for (const auto& candidate : animations) {
        const bool selected = candidate == current;
        const std::string path = candidate->getFullPath();
        if (ImGui::Selectable(path.c_str(), selected) && !selected) {
            if (history) history->execute(std::make_unique<SetHumanoidAnimationCommand>(
                humanoid, slot, currentState,
                HumanoidAnimationReferenceState{candidate, {}}));
            else setAnimationForSlot(*humanoid, slot, candidate);
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

static void drawAnimationInspector(const std::shared_ptr<Animation>& animation) {
    ImGui::LabelText("Source", "%s", animation->getSourceName().c_str());
    ImGui::LabelText("LoadStatus", "%s", animation->getLoadStatusName().c_str());
    ImGui::LabelText("UsingBuiltInFallback", "%s",
                     animation->isUsingBuiltInFallback() ? "true" : "false");
    ImGui::TextUnformatted("Message");
    ImGui::TextWrapped("%s", animation->getLoadMessage().empty()
        ? "(none)" : animation->getLoadMessage().c_str());
}

} // namespace

PropertiesPanel::PropertiesPanel()
    : EditorPanel("Properties") {}

void PropertiesPanel::drawObjectValueRef(const char* label, const std::shared_ptr<Instance>& inst)
{
    auto* ov = static_cast<ObjectValue*>(inst.get());
    static std::unordered_map<std::string, std::string> s_before;
    std::string key = "objval_" + inst->Name;

    bool isPickingThis = m_picker && m_picker->active && m_picker->constraint == inst.get();
    bool anyPicking     = m_picker && m_picker->active;

    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    float btnW  = 46.0f;
    float space = ImGui::GetStyle().ItemSpacing.x;
    float fieldW = ImGui::GetContentRegionAvail().x - btnW - space;
    if (fieldW < 60.0f) fieldW = 60.0f;
    ImGui::SetNextItemWidth(fieldW);

    char buf[512] = {};
    strncpy(buf, ov->m_targetPathName.c_str(), sizeof(buf) - 1);
    std::string inputId = "##" + key;
    ImGui::InputText(inputId.c_str(), buf, sizeof(buf));
    if (ImGui::IsItemActivated()) s_before[key] = ov->m_targetPathName;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string before = s_before[key];
        std::string after(buf);
        YAML::Node n; n = after;
        inst->setProperty("Value", n);
        if (before != after && m_history)
            m_history->record(std::make_unique<SetConstraintCubeNameCommand>(inst, "Value", before, after));
    }

    ImGui::SameLine();

    if (isPickingThis) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button(("Cancel##pick_" + key).c_str(), ImVec2(btnW, 0)))
            m_picker->active = false;
        ImGui::PopStyleColor();
    } else {
        if (anyPicking) ImGui::BeginDisabled();
        if (ImGui::Button(("Pick##" + key).c_str(), ImVec2(btnW, 0)) && m_picker) {
            m_picker->active          = true;
            m_picker->pickAttachment  = false;
            m_picker->pickAnyInstance = true;
            m_picker->pickClassName.clear();
            m_picker->prop            = "Value";
            m_picker->constraint      = inst.get();
            m_picker->onPick = [inst, hist = m_history](std::shared_ptr<Instance> picked) {
                auto* ovp = static_cast<ObjectValue*>(inst.get());
                std::string before = ovp->m_targetPathName;
                ovp->setTarget(picked);
                std::string after = ovp->m_targetPathName;
                if (hist && before != after)
                    hist->record(std::make_unique<SetConstraintCubeNameCommand>(inst, "Value", before, after));
            };
        }
        if (anyPicking) ImGui::EndDisabled();
    }
}


void PropertiesPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }
    if (readOnly) ImGui::BeginDisabled();

    Instance* inst = selectedInstance ? *selectedInstance : nullptr;
    if (selectedInstances && selectedInstances->size() > 1) {
        renderMultiInspector(*selectedInstances, m_history, m_picker);
        if (readOnly) ImGui::EndDisabled();
        ImGui::End();
        return;
    }
    // ツリーから除去済み（Parent expired）なインスタンスは選択解除
    // System はツリーのルートで元々親を持たない（Parent が常に expired）ため対象外にする
    if (inst && inst->Parent.expired() && !inst->IsA("System")) {
        *selectedInstance = nullptr;
        inst = nullptr;
    }

    if (!inst) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::NothingSelected));
        if (readOnly) ImGui::EndDisabled();
        ImGui::End();
        return;
    }

    if (m_picker && m_picker->active) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
        if (!m_picker->pickClassName.empty())
            ImGui::Text("Select %s", m_picker->pickClassName.c_str());
        else ImGui::TextUnformatted(m_picker->pickAnyInstance
            ? Loc::t(Loc::LocKey::PickerPromptAny)
            : m_picker->pickAttachment
                ? Loc::t(Loc::LocKey::PickerPromptAttachment)
                : Loc::t(Loc::LocKey::PickerPromptCube));
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ---- 基本情報 ----
    ImGui::SeparatorText("Instance");
    ImGui::LabelText("ClassName", "%s", inst->getClassName().c_str());
    ImGui::LabelText("Path",      "%s", inst->getFullPath().c_str());

    static std::weak_ptr<Instance> s_nameTarget;
    static std::string s_nameBefore;
    static char s_nameBuf[256] = {};
    auto commitName = [&]() {
        if (auto target = s_nameTarget.lock(); target && !target->isRuntimeNameLocked()) {
            std::string requestedName(s_nameBuf);
            if (s_nameBefore != requestedName) {
                target->renameTo(requestedName);
                std::string after = target->Name;
                if (m_history && s_nameBefore != after) {
                    m_history->record(std::make_unique<RenameInstanceCommand>(
                        target, s_nameBefore, after));
                }
            }
        }
    };
    auto clearNameEdit = [&]() {
        s_nameTarget.reset();
        s_nameBefore.clear();
    };
    if (auto target = s_nameTarget.lock(); target && target.get() != inst) {
        commitName();
        clearNameEdit();
    }
    if (s_nameTarget.expired()) {
        strncpy(s_nameBuf, inst->Name.c_str(), sizeof(s_nameBuf) - 1);
        s_nameBuf[sizeof(s_nameBuf) - 1] = '\0';
    }

    const bool nameLocked = inst->isRuntimeNameLocked();
    if (nameLocked) ImGui::BeginDisabled();
    bool submit = ImGui::InputText("Name", s_nameBuf, sizeof(s_nameBuf),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    bool activated = ImGui::IsItemActivated();
    bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    bool deactivated = ImGui::IsItemDeactivated();
    if (nameLocked) ImGui::EndDisabled();

    if (activated && !nameLocked) {
        s_nameTarget = inst->shared_from_this();
        s_nameBefore = inst->Name;
    }
    if (submit || deactivatedAfterEdit) {
        commitName();
    }
    if (deactivated) {
        clearNameEdit();
    }

    // Instance 側 schema の基底→派生集合だけを描画する。通常 property の
    // class-specific UI はここへ流れ、特殊な adapter/action だけが下に残る。
    renderSchemaInspector(inst, inst->getClassName().c_str(), m_history, m_picker);

    // ---- SurfaceMark (空間からの投影画像) ----
    if (inst->getClassName() == "SurfaceMark") {
        ImGui::SeparatorText("SurfaceMark");
        ImGui::TextDisabled("Size: X = Width, Y = Height, Z = Projection Depth");
        auto mark = static_cast<SurfaceMark*>(inst);
        auto commitFilter = [this, mark](const std::vector<std::shared_ptr<Instance>>& before,
                                         const std::vector<std::string>& beforePaths) {
            auto target = std::static_pointer_cast<SurfaceMark>(mark->shared_from_this());
            std::vector<std::shared_ptr<Instance>> after;
            for (const auto& weak : mark->getFilterInstances()) after.push_back(weak.lock());
            if (m_history) m_history->record(std::make_unique<SetSurfaceMarkFilterCommand>(
                target, before, beforePaths, after, mark->getFilterPaths()));
        };
        ImGui::SeparatorText("SurfaceMark Filter");
        if (ImGui::Button("Add Instance##surfacemarkfilter") && m_picker) {
            m_picker->active = true; m_picker->pickAnyInstance = true;
            m_picker->pickAttachment = false; m_picker->pickClassName.clear();
            m_picker->prop = "FilterInstances"; m_picker->constraint = inst;
            m_picker->onPick = [mark, commitFilter](std::shared_ptr<Instance> picked) {
                if (!picked) return;
                for (const auto& weak : mark->getFilterInstances())
                    if (auto existing = weak.lock(); existing.get() == picked.get()) return;
                std::vector<std::shared_ptr<Instance>> before;
                for (const auto& weak : mark->getFilterInstances()) before.push_back(weak.lock());
                const auto beforePaths = mark->getFilterPaths();
                auto after = before;
                after.push_back(picked);
                auto paths = beforePaths;
                paths.push_back(picked->getWorkspaceRelativePath());
                mark->setFilterState(after, paths);
                commitFilter(before, beforePaths);
            };
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##surfacemarkfilter") && !mark->getFilterPaths().empty()) {
            std::vector<std::shared_ptr<Instance>> before;
            for (const auto& weak : mark->getFilterInstances()) before.push_back(weak.lock());
            const auto beforePaths = mark->getFilterPaths();
            mark->setFilterState({}, {});
            commitFilter(before, beforePaths);
        }
        for (size_t i = 0; i < mark->getFilterPaths().size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(mark->getFilterPaths()[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                std::vector<std::shared_ptr<Instance>> before;
                for (const auto& weak : mark->getFilterInstances()) before.push_back(weak.lock());
                const auto beforePaths = mark->getFilterPaths();
                auto after = before;
                if (i < after.size()) after.erase(after.begin() + static_cast<std::ptrdiff_t>(i));
                auto paths = beforePaths;
                paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(i));
                mark->setFilterState(after, paths);
                commitFilter(before, beforePaths);
                ImGui::PopID(); break;
            }
            ImGui::PopID();
        }

    }

    // ---- MeshCube ----
    if (inst->getClassName() == "MeshCube") {
        MeshCube* mc = static_cast<MeshCube*>(inst);
        ImGui::SeparatorText("MeshCube");
        if (ImGui::Button(locId(Loc::LocKey::RegenerateUVButton, "##meshcubeuvregen").c_str())) {
            ImGui::OpenPopup("###MeshCubeUVRegenConfirm");
        }
        std::string uvRegenPopupTitle = locId(Loc::LocKey::RegenerateUVConfirmTitle, "###MeshCubeUVRegenConfirm");
        if (ImGui::BeginPopupModal(uvRegenPopupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Loc::t(Loc::LocKey::RegenerateUVConfirmLine1));
            ImGui::Text("%s", Loc::t(Loc::LocKey::RegenerateUVConfirmLine2));
            ImGui::Separator();
            if (ImGui::Button(Loc::t(Loc::LocKey::RegenerateConfirmButton), ImVec2(120, 0))) {
                if (mc->regenerateUV()) {
                    mc->uploadToGPU();
                } else {
                    RCBN_WARN("MeshCube: UV再生成に失敗しました: " << mc->MeshFile);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Loc::t(Loc::LocKey::Cancel), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_decalPlace) {
            ImGui::Checkbox("Decal配置モード", &m_decalPlace->active);
        }
    }

    // ---- Sound actions ----
    if (inst->getClassName() == "Sound") {
        Sound* snd = static_cast<Sound*>(inst);
        ImGui::SeparatorText("Sound");
        // 再生時間スクラバと再生操作は property ではなく EditorAction。
        {
            float len = snd->getLength();
            if (len > 0.0f) {
                float cur = snd->getPlaybackTime();
                if (ImGui::SliderFloat("Time", &cur, 0.0f, len, "%.2f s"))
                    snd->seekSeconds(cur);
                ImGui::Text("%d:%02d / %d:%02d",
                    (int)cur / 60, (int)cur % 60, (int)len / 60, (int)len % 60);
            }
        }

        if (ImGui::Button(Loc::t(Loc::LocKey::PlayButton)))  snd->play();
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::StopButton)))  snd->stop();
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::ResetButton))) snd->reset();
    }

    // ---- Script ----
    if (inst->IsA("Script")) {
        Script* sc = static_cast<Script*>(inst);
        ImGui::SeparatorText("Script actions");
        if (ImGui::Button(Loc::t(Loc::LocKey::OpenExternalEditor)) && !sc->Path.empty()) {
            getPlatform().revealInFileManager(sc->Path);
        }

        ImGui::BeginDisabled();
        bool aborted = sc->Aborted;
        ImGui::Checkbox("Aborted", &aborted);
        ImGui::EndDisabled();

        if (ImGui::Button("Restart")) {
            sc->restart();
        }
    }

    // ---- Decal ----
    if (inst->getClassName() == "Decal") {
        Decal* dcl = static_cast<Decal*>(inst);
        auto dclSp = std::static_pointer_cast<Decal>(inst->shared_from_this());
        ImGui::SeparatorText("Decal");

        // 親がMeshCubeかどうかでFace ComboとUVCenter/UVRadiusを切り替える
        bool parentIsMeshCube = false;
        {
            auto par = dcl->Parent.lock();
            if (par && par->IsA("MeshCube")) parentIsMeshCube = true;
        }

        if (parentIsMeshCube) {
            static const char* modeItems[] = { "UV (Free)", "Face (Full)" };
            int modeIdx = static_cast<int>(dcl->Mode);
            if (ImGui::Combo("Mode", &modeIdx, modeItems, 2)) {
                DecalMode newMode = static_cast<DecalMode>(modeIdx);
                if (newMode != dcl->Mode) {
                    DecalMode oldMode = dcl->Mode;
                    dcl->setMode(newMode);
                    if (m_history)
                        m_history->record(std::make_unique<SetDecalModeCommand>(dclSp, oldMode, newMode));
                }
            }

            if (dcl->Mode == DecalMode::UV) {
                // UVCenter/UVRadius with undo (MeshCube配下専用)
                static Vector2 s_dclUVCenterBefore;
                static float   s_dclUVRadiusBefore;

                float center[2] = { dcl->UVCenter.x, dcl->UVCenter.y };
                bool centerChanged = ImGui::DragFloat2("UVCenter", center, 0.01f, 0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemActivated()) { s_dclUVCenterBefore = dcl->UVCenter; s_dclUVRadiusBefore = dcl->UVRadius; }
                if (centerChanged) dcl->setUVCenter(Vector2(center[0], center[1]));
                if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                    m_history->record(std::make_unique<SetDecalUVCommand>(
                        dclSp, s_dclUVCenterBefore, s_dclUVRadiusBefore, dcl->UVCenter, dcl->UVRadius));
                }

                float radius = dcl->UVRadius;
                bool radiusChanged = ImGui::DragFloat("UVRadius", &radius, 0.005f, 0.01f, 1.0f, "%.3f");
                if (ImGui::IsItemActivated()) { s_dclUVCenterBefore = dcl->UVCenter; s_dclUVRadiusBefore = dcl->UVRadius; }
                if (radiusChanged) dcl->setUVRadius(radius);
                if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                    m_history->record(std::make_unique<SetDecalUVCommand>(
                        dclSp, s_dclUVCenterBefore, s_dclUVRadiusBefore, dcl->UVCenter, dcl->UVRadius));
                }
            } else {
                // Face combo with undo
                static const char* faceItems[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };
                int faceIdx = static_cast<int>(dcl->face);
                if (ImGui::Combo("Face", &faceIdx, faceItems, 6)) {
                    Face newFace = static_cast<Face>(faceIdx);
                    if (newFace != dcl->face) {
                        Face oldFace = dcl->face;
                        dcl->setFace(newFace);
                        if (m_history)
                            m_history->record(std::make_unique<SetDecalFaceCommand>(dclSp, oldFace, newFace));
                    }
                }
            }
        } else {
            // Face combo with undo
            static const char* faceItems[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };
            int faceIdx = static_cast<int>(dcl->face);
            if (ImGui::Combo("Face", &faceIdx, faceItems, 6)) {
                Face newFace = static_cast<Face>(faceIdx);
                if (newFace != dcl->face) {
                    Face oldFace = dcl->face;
                    dcl->setFace(newFace);
                    if (m_history)
                        m_history->record(std::make_unique<SetDecalFaceCommand>(dclSp, oldFace, newFace));
                }
            }
        }

    }

    // ---- User ----
    if (inst->getClassName() == "User") {
        User* usr = static_cast<User*>(inst);
        auto usrSp = std::static_pointer_cast<User>(inst->shared_from_this());
        ImGui::SeparatorText("User");

        // Custom mouse cursors.  The image slots are intentionally explicit so
        // their serialized names remain stable (CursorImages[0..9]).
        {
            const bool cursorReadOnly = readOnly;
            ImGui::BeginDisabled(cursorReadOnly);
            const char* cursorTypes[] = { "Default", "Type1", "Type2", "Type3", "Type4", "Type5",
                                          "Type6", "Type7", "Type8", "Type9", "Type10" };
            int type = static_cast<int>(usr->getCursorType());
            if (ImGui::Combo("CursorType", &type, cursorTypes, 11)) {
                const int before = static_cast<int>(usr->getCursorType());
                usr->setCursorType(static_cast<User::CursorType>(type));
                if (m_history && before != type)
                    m_history->record(std::make_unique<SetUserCursorCommand>(
                        usrSp, SetUserCursorCommand::Kind::Type, 0, before, type, "", ""));
            }
            for (std::size_t i = 0; i < User::CURSOR_IMAGE_SLOT_COUNT; ++i) {
                const auto& slot = usr->getCursorImageSlot(i);
                static std::array<int, User::CURSOR_IMAGE_SLOT_COUNT> hotspotBeforeX{};
                static std::array<int, User::CURSOR_IMAGE_SLOT_COUNT> hotspotBeforeY{};
                static std::array<int, User::CURSOR_IMAGE_SLOT_COUNT> sizeBefore{};
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("Type%zu", i + 1);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - 120.0f));
                ImGui::LabelText("##cursor_path", "%s", slot.contentPath.empty() ? "(none)" : slot.contentPath.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Browse")) {
                    const std::string selected = getPlatform().openFileDialog({{
                        "Cursor image", "*.png;*.jpg;*.jpeg;*.bmp;*.tga"}});
                    if (!selected.empty()) {
                        const std::string path = toProjectRelative(selected);
                        if (path != slot.contentPath) {
                            if (m_history) m_history->execute(std::make_unique<SetUserCursorCommand>(
                                usrSp, SetUserCursorCommand::Kind::Path, i, 0, 0,
                                slot.contentPath, path));
                            else usr->setCursorImagePath(i, path);
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(slot.contentPath.empty());
                if (ImGui::Button("Clear")) {
                    if (m_history) m_history->execute(std::make_unique<SetUserCursorCommand>(
                        usrSp, SetUserCursorCommand::Kind::Path, i, 0, 0, slot.contentPath, ""));
                    else usr->setCursorImagePath(i, "");
                }
                ImGui::EndDisabled();
                int hotspotX = slot.hotspotX;
                int hotspotY = slot.hotspotY;
                const int originalX = slot.hotspotX;
                const int originalY = slot.hotspotY;
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputInt("HotspotX", &hotspotX)) {
                    hotspotX = std::max(0, hotspotX);
                    usr->setCursorHotspotX(i, hotspotX);
                }
                if (ImGui::IsItemActivated()) hotspotBeforeX[i] = originalX;
                if (ImGui::IsItemDeactivatedAfterEdit() && m_history && hotspotX != hotspotBeforeX[i])
                    m_history->execute(std::make_unique<SetUserCursorCommand>(
                        usrSp, SetUserCursorCommand::Kind::HotspotX, i,
                        hotspotBeforeX[i], hotspotX, "", ""));
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputInt("HotspotY", &hotspotY)) {
                    hotspotY = std::max(0, hotspotY);
                    usr->setCursorHotspotY(i, hotspotY);
                }
                if (ImGui::IsItemActivated()) hotspotBeforeY[i] = originalY;
                if (ImGui::IsItemDeactivatedAfterEdit() && m_history && hotspotY != hotspotBeforeY[i])
                    m_history->execute(std::make_unique<SetUserCursorCommand>(
                        usrSp, SetUserCursorCommand::Kind::HotspotY, i,
                        hotspotBeforeY[i], hotspotY, "", ""));
                int size = slot.size;
                const int originalSize = slot.size;
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputInt("Size(px)", &size)) {
                    size = std::clamp(size, 1, User::MAX_CURSOR_SIZE);
                    usr->setCursorSize(i, size);
                }
                if (ImGui::IsItemActivated()) sizeBefore[i] = originalSize;
                if (ImGui::IsItemDeactivatedAfterEdit() && m_history && size != sizeBefore[i])
                    m_history->execute(std::make_unique<SetUserCursorCommand>(
                        usrSp, SetUserCursorCommand::Kind::Size, i,
                        sizeBefore[i], size, "", ""));
                ImGui::PopID();
            }
            ImGui::EndDisabled();
        }

        // Current slot index (read-only)
        ImGui::LabelText("CurrentSlotIndex", "%d", usr->currentSlotIndex);

        // Inventory (reference)
        if (usr->Inventory) {
            ImGui::LabelText("Inventory", "%s", usr->Inventory->Name.c_str());
        }

        // Character (reference, read-only): Luau経由での動的差し替えが主目的のため編集UIは提供しない
        ImGui::LabelText("Character", "%s", usr->character ? usr->character->Name.c_str() : "(none)");

        // Tool slots (read-only)
        ImGui::SeparatorText("Tool Slots");
        for (int i = 0; i < 10; i++) {
            std::string slotLabel = "Slot " + std::to_string(i);
            ImGui::LabelText(slotLabel.c_str(), "%s", usr->Slots[i] ? usr->Slots[i]->Name.c_str() : "(empty)");
        }
    }

    // ---- Terrain ----
    if (inst->getClassName() == "Terrain") {
        Terrain* terrain = static_cast<Terrain*>(inst);
        auto terrSp = std::static_pointer_cast<Terrain>(inst->shared_from_this());
        ImGui::SeparatorText("Terrain");

        // データ保存先ディレクトリ（リージョンファイルの置き場所）— フォルダ参照
        ImGui::LabelText("DataPath", "%s", terrain->DataPath.c_str());
        if (ImGui::Button("Use Existing...##terraindp")) {
            std::string folder = getPlatform().openFolderDialog();
            if (!folder.empty()) {
                std::string before = terrain->DataPath;
                YAML::Node node; node = folder;
                terrain->setProperty("DataPath", node);
                if (m_history) m_history->record(std::make_unique<SetTerrainStringCommand>(terrSp, "DataPath", before, terrain->DataPath));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Create New...##terraindp"))
            ImGui::OpenPopup("###TerrainDataPathCreate");
        if (ImGui::BeginPopupModal(
                "Create Terrain Data###TerrainDataPathCreate", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            static char directoryName[128] = "Terrain";
            ImGui::TextUnformatted("Terrain directory name");
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("##terrainDirectoryName", directoryName,
                             sizeof(directoryName));
            if (ImGui::Button("Select Parent Folder", ImVec2(150, 0))) {
                const std::string parent = getPlatform().openFolderDialog();
                if (!parent.empty() && directoryName[0] != '\0') {
                    std::filesystem::path target =
                        std::filesystem::path(parent) / directoryName;
                    std::error_code error;
                    const bool occupied = std::filesystem::exists(target, error) &&
                        !std::filesystem::is_empty(target, error);
                    if (occupied) {
                        RCBN_WARN("Terrain directory is not empty; refusing to overwrite: "
                                  << target.string());
                    } else if ((!std::filesystem::create_directories(target, error) && error)) {
                        RCBN_WARN("Failed to create Terrain directory: "
                                  << error.message());
                    } else {
                        const std::string before = terrain->DataPath;
                        YAML::Node node;
                        node = target.string();
                        terrain->setProperty("DataPath", node);
                        if (m_history && before != terrain->DataPath)
                            m_history->record(std::make_unique<SetTerrainStringCommand>(
                                terrSp, "DataPath", before, terrain->DataPath));
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(locId(Loc::LocKey::TerrainRandomize, "##terrainseed").c_str())) {
            int before = terrain->Seed;
            std::random_device rd;
            const int after = static_cast<int>(rd());
            YAML::Node node;
            node = after;
            terrain->setProperty("Seed", node);
            if (m_history) m_history->record(std::make_unique<SetTerrainIntCommand>(terrSp, "Seed", before, after));
        }

        if (ImGui::Button(locId(Loc::LocKey::TerrainRegenerateButton, "##terrainregen").c_str())) {
            ImGui::OpenPopup("###TerrainRegenConfirm");
            m_terrainRegenOpenedAt = ImGui::GetTime();
        }
        std::string terrainPopupTitle = locId(Loc::LocKey::TerrainRegenConfirmTitle, "###TerrainRegenConfirm");
        if (ImGui::BeginPopupModal(terrainPopupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Loc::t(Loc::LocKey::TerrainRegenConfirmLine1));
            ImGui::Text("%s", Loc::t(Loc::LocKey::TerrainRegenConfirmLine2));
            ImGui::Separator();
            if (EditorUi::dangerButton(Loc::t(Loc::LocKey::RegenerateConfirmButton), m_terrainRegenOpenedAt)) {
                if (terrain->streamer) {
                    terrain->streamer->regenerate(static_cast<uint32_t>(terrain->Seed), terrain->Flat);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (EditorUi::safeButton(Loc::t(Loc::LocKey::Cancel))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ---- Skybox ----
    if (inst->getClassName() == "Skybox") {
        Skybox* sb = static_cast<Skybox*>(inst);
        auto sbSp = std::static_pointer_cast<Skybox>(inst->shared_from_this());

        ImGui::SeparatorText("Skybox Faces");
        static const char* s_skyboxLabels[] = {
            "Right (+X)", "Left (-X)", "Top (+Y)", "Bottom (-Y)", "Front (+Z)", "Back (-Z)"
        };
        for (int i = 0; i < 6; i++) {
            ImGui::LabelText(s_skyboxLabels[i], "%s",
                sb->skyboxPaths[i].empty() ? "(none)" : sb->skyboxPaths[i].c_str());
            std::string btnId = locId(Loc::LocKey::Browse, "##skybox") + std::to_string(i);
            if (ImGui::Button(btnId.c_str())) {
                std::string path = getPlatform().openFileDialog({{"Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga"}});
                if (!path.empty()) {
                    std::string oldPath = sb->skyboxPaths[i];
                    sb->setSkyboxPath(i, path);
                    if (m_history)
                        m_history->record(std::make_unique<SetSkyboxFaceCommand>(sbSp, i, oldPath, path));
                }
            }
        }
    }

    // ---- Humanoid（スキーマ駆動。プロパティ追加はスキーマに1行足すだけ） ----
    if (inst->getClassName() == "Humanoid") {
        auto humanoid = std::static_pointer_cast<Humanoid>(inst->shared_from_this());
        ImGui::SeparatorText("Animation References");
        drawHumanoidAnimationReference("WalkAnimation", humanoid,
                                       HumanoidAnimationSlot::Walk, m_history);
        drawHumanoidAnimationReference("JumpAnimation", humanoid,
                                       HumanoidAnimationSlot::Jump, m_history);
        drawHumanoidAnimationReference("EquipAnimation", humanoid,
                                       HumanoidAnimationSlot::Equip, m_history);
    }

    if (inst->getClassName() == "Animation") {
        ImGui::SeparatorText("Animation");
        drawAnimationInspector(std::static_pointer_cast<Animation>(inst->shared_from_this()));
    }

    // ---- GUI の特殊 adapter ----
    if (inst->getClassName() == "TextLabel") {
        drawFontSelection(static_cast<ScreenGuiObject*>(inst), m_history, m_picker);
    }
    if (inst->getClassName() == "TextButton") {
        drawFontSelection(static_cast<ScreenGuiObject*>(inst), m_history, m_picker);
    }
    if (inst->getClassName() == "NumberValue") {
        ImGui::SeparatorText("NumberValue");
        auto* nv = static_cast<NumberValue*>(inst);
        static std::unordered_map<std::string, double> s_numBefore;
        std::string key = "numval_" + inst->Name;

        double v = nv->Value;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputDouble("##NumberValue", &v)) {
            YAML::Node n; n = v;
            inst->setProperty("Value", n);
        }
        if (ImGui::IsItemActivated()) s_numBefore[key] = nv->Value;
        if (ImGui::IsItemDeactivatedAfterEdit() && m_history && s_numBefore[key] != nv->Value) {
            m_history->record(std::make_unique<SetNumberValueCommand>(
                inst->shared_from_this(), s_numBefore[key], nv->Value));
        }
    }
    if (inst->getClassName() == "ObjectValue") {
        ImGui::SeparatorText("ObjectValue");
        drawObjectValueRef("Value", std::static_pointer_cast<Instance>(inst->shared_from_this()));
    }
    if (readOnly) ImGui::EndDisabled();
    ImGui::End();
}
