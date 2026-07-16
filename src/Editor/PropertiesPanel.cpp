#include <Editor/PropertiesPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/UiHelpers.hpp>
#include <Editor/Localization.hpp>
#include <Core/Physics.hpp>
#include <Core/User.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/FileRef.hpp>
#include <Instances/Decal.hpp>
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
#include <Instances/Rod.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Tool.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Humanoid.hpp>
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
#include <include/imgui/imgui.h>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ===================================================
//  PropertiesPanel 実装
// ===================================================

// ===================================================
//  スキーマ駆動インスペクタ
//  PropertyRegistry に登録済みのクラスは、ここが表を辿って自動でウィジェットを
//  描画し、編集を汎用 SetPropertyCommand として記録する（Undo / dirty 対応）。
//  → スキーマに1行足すだけでインスペクタに反映され、エディター取り残しを防ぐ。
// ===================================================
// ローカライズ済みラベル + ImGui ID サフィックス（"##foo"）を連結するヘルパー
static std::string locId(Loc::LocKey key, const char* idSuffix) {
    return std::string(Loc::t(key)) + idSuffix;
}

static void renderSchemaInspector(Instance* inst, const char* className, CommandHistory* history) {
    static PropValue s_before;  // 編集開始時の値（同時編集は1つなので単一でよい）
    // own-only（既存の per-level エディターブロック構造を保つ。基底は各 IsA ブロックで描画）
    for (const auto& d : PropertyRegistry::schemaFor(className)) {
        const PropertyDesc* dp = &d;
        if (d.kind != PropKind::Field || !d.editable || !d.get) continue;
        const bool readOnly = !d.set;
        std::string label(d.name);
        // liveSet があればドラッグ中はそちらを使う（軽量反映）。無ければ set をそのまま使う
        auto applyLive = [&d](Instance* o, const PropValue& v) {
            if (d.liveSet) d.liveSet(o, v); else d.set(o, v);
        };
        if (!d.separator.empty()) ImGui::SeparatorText(d.separator.data());
        ImGui::PushID(static_cast<int>(reinterpret_cast<std::uintptr_t>(dp)));
        if (readOnly) ImGui::BeginDisabled();

        PropValue cur = d.get(inst);
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
        if (ImGui::IsItemActivated())
            s_before = PropertyRegistry::readValue(inst, d);
        if (ImGui::IsItemDeactivatedAfterEdit() && history) {
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
static void renderMultiInspector(const std::vector<Instance*>& sel, CommandHistory* history) {
    static std::vector<PropValue> s_multiBefore;

    std::vector<Instance*> valid;
    for (Instance* inst : sel) {
        if (!inst) continue;
        if (inst->Parent.expired() && !inst->IsA("System")) continue;
        valid.push_back(inst);
    }

    ImGui::Text(Loc::t(Loc::LocKey::MultiSelectedCount), (int)valid.size());
    if (valid.size() < 2) return;

    // 各インスタンスのスキーマ集合（自クラス優先、BaseCube配下なら共通プロパティを補完）
    auto buildSchema = [](Instance* inst) {
        std::vector<const PropertyDesc*> result;
        std::unordered_map<std::string_view, bool> seen;
        for (const auto* d : PropertyRegistry::collectSchema(inst->getClassName())) {
            if (!seen.count(d->name)) { result.push_back(d); seen[d->name] = true; }
        }
        if (inst->IsA("BaseCube")) {
            for (const auto* d : PropertyRegistry::collectSchema("BaseCube")) {
                if (!seen.count(d->name)) { result.push_back(d); seen[d->name] = true; }
            }
        }
        return result;
    };

    std::vector<std::vector<const PropertyDesc*>> perInstSchema;
    std::vector<std::unordered_map<std::string_view, const PropertyDesc*>> perInstMap;
    perInstSchema.reserve(valid.size());
    perInstMap.reserve(valid.size());
    for (Instance* inst : valid) {
        auto s = buildSchema(inst);
        std::unordered_map<std::string_view, const PropertyDesc*> m;
        for (const auto* d : s) m[d->name] = d;
        perInstMap.push_back(std::move(m));
        perInstSchema.push_back(std::move(s));
    }

    // 共通プロパティの積集合を、先頭インスタンスのスキーマ順に走査する
    for (const PropertyDesc* d0 : perInstSchema[0]) {
        if (d0->kind != PropKind::Field || !d0->editable || !d0->get || !d0->set) continue;

        std::vector<std::pair<Instance*, const PropertyDesc*>> rows;
        rows.emplace_back(valid[0], d0);
        bool commonAcrossAll = true;
        for (size_t i = 1; i < valid.size(); ++i) {
            auto it = perInstMap[i].find(d0->name);
            if (it == perInstMap[i].end()) { commonAcrossAll = false; break; }
            const PropertyDesc* di = it->second;
            if (di->type != d0->type || di->kind != PropKind::Field || !di->editable || !di->get || !di->set) {
                commonAcrossAll = false; break;
            }
            rows.emplace_back(valid[i], di);
        }
        if (!commonAcrossAll) continue;

        std::string name(d0->name);

        if (!d0->separator.empty()) ImGui::SeparatorText(d0->separator.data());
        ImGui::PushID(name.c_str());

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
            if (!(curVals[i] == curVals[0])) { mixed = true; break; }
        }
        PropValue cur = curVals[0];

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
                if (changed) applyLiveAll(PropValue(v));
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
        if (ImGui::IsItemActivated()) {
            s_multiBefore.clear();
            for (auto& [inst, d] : rows) s_multiBefore.push_back(d->get(inst));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && history) {
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

PropertiesPanel::PropertiesPanel()
    : EditorPanel("Properties") {}

// ===================================================
//  Vector3 一括入力 + 展開式フィールド
//  collapsed: InputText "x, y, z"  [▼]
//  expanded : DragFloat × 3         [▲]
// ===================================================
static void drawVec3Field(const char* id,
                          Vector3& val,
                          float speed, float minVal, float maxVal,
                          std::shared_ptr<Spatial> sp,
                          const std::string& prop,
                          CommandHistory* history)
{
    static std::unordered_map<std::string, bool>    s_exp;
    static std::unordered_map<std::string, Vector3> s_before;
    bool& expanded = s_exp[id];

    // 折りたたみ: InputText "x, y, z"
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", val.x, val.y, val.z);

        // [+/-] と [丸] の2ボタン分の幅は言語によって変わる（例: "丸" vs "Round"）ため、
        // 固定マジックナンバーではなく実際のラベル幅から動的に算出する。
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* expLabel = expanded ? "-" : "+";
        float expBtnW   = ImGui::CalcTextSize(expLabel).x + style.FramePadding.x * 2.0f;
        float roundBtnW = ImGui::CalcTextSize(Loc::t(Loc::LocKey::RoundButton)).x + style.FramePadding.x * 2.0f;
        float reserved  = expBtnW + roundBtnW + style.ItemSpacing.x * 2.0f;

        float w = ImGui::GetContentRegionAvail().x - reserved;
        if (w < 60.0f) w = 60.0f;
        ImGui::SetNextItemWidth(w);

        std::string txtId = std::string("##txt_") + id;
        if (ImGui::InputText(txtId.c_str(), buf, sizeof(buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            float x = val.x, y = val.y, z = val.z;
            if (sscanf(buf, "%f , %f , %f", &x, &y, &z) == 3 ||
                sscanf(buf, "%f,%f,%f",     &x, &y, &z) == 3) {
                Vector3 newVal(x, y, z);
                if (history && sp) {
                    history->execute(std::make_unique<SetVec3Command>(sp, prop, val, newVal));
                } else {
                    val = newVal;
                }
            }
        }
        ImGui::SameLine();
        std::string btnId = std::string(expanded ? "-##col_" : "+##exp_") + id;
        if (ImGui::SmallButton(btnId.c_str())) expanded = !expanded;

        // 整数に丸めるボタン: 各成分を最近傍整数に丸めて適用（Undo 連携）
        ImGui::SameLine();
        std::string roundId = std::string(Loc::t(Loc::LocKey::RoundButton)) + "##round_" + id;
        if (ImGui::SmallButton(roundId.c_str())) {
            Vector3 newVal(
                std::clamp(std::round(val.x), minVal, maxVal),
                std::clamp(std::round(val.y), minVal, maxVal),
                std::clamp(std::round(val.z), minVal, maxVal));
            if (history && sp) {
                history->execute(std::make_unique<SetVec3Command>(sp, prop, val, newVal));
            } else {
                val = newVal;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Loc::t(Loc::LocKey::RoundTooltip));
    }

    // 展開: DragFloat3
    if (expanded) {
        ImGui::Indent(12.0f);
        ImGui::PushID(id);

        std::string key = std::string(id) + "_before";
        float arr[3] = { val.x, val.y, val.z };
        bool changed = ImGui::DragFloat3("##drag", arr, speed, minVal, maxVal);

        if (ImGui::IsItemActivated()) s_before[key] = val;

        if (changed) {
            Vector3 newVal(arr[0], arr[1], arr[2]);
            if (sp && sp->IsA("BaseCube")) {
                BaseCube* bc = static_cast<BaseCube*>(sp.get());
                if (prop == "Position") bc->teleportTo(newVal);
                else if (prop == "Size") bc->setSize(newVal);
            } else {
                val = newVal;  // 非 BaseCube の Spatial（cframe.Position / Size を直接更新）
            }
        }

        if (ImGui::IsItemDeactivatedAfterEdit() && history && sp) {
            Vector3 after(arr[0], arr[1], arr[2]);
            history->record(std::make_unique<SetVec3Command>(sp, prop, s_before[key], after));
        }

        ImGui::PopID();
        ImGui::Unindent(12.0f);
    }
}

void PropertiesPanel::drawConstraintCubeRef(const char* label, std::string& nameRef,
                                             const char* prop,
                                             const std::shared_ptr<Instance>& inst)
{
    static std::unordered_map<std::string, std::string> s_before;
    std::string key = std::string(prop) + "_" + inst->Name;

    bool isPickingThis = m_picker && m_picker->active
                      && m_picker->constraint == inst.get()
                      && m_picker->prop == prop;
    bool anyPicking    = m_picker && m_picker->active;

    // ラベルを左に手動描画し、InputText は ## ID で幅を正確に制御する
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    float btnW  = 46.0f;
    float space = ImGui::GetStyle().ItemSpacing.x;
    float fieldW = ImGui::GetContentRegionAvail().x - btnW - space;
    if (fieldW < 60.0f) fieldW = 60.0f;
    ImGui::SetNextItemWidth(fieldW);

    char buf[512] = {};
    strncpy(buf, nameRef.c_str(), sizeof(buf) - 1);
    std::string inputId = "##cuberef_" + key;
    ImGui::InputText(inputId.c_str(), buf, sizeof(buf));
    if (ImGui::IsItemActivated()) s_before[key] = nameRef;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string after(buf);
        if (nameRef != after && m_history)
            m_history->record(std::make_unique<SetConstraintCubeNameCommand>(
                inst, prop, nameRef, after));
        nameRef = after;
    }

    ImGui::SameLine();

    if (isPickingThis) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button(("Cancel##pick_" + key).c_str(), ImVec2(btnW, 0)))
            m_picker->active = false;
        ImGui::PopStyleColor();
    } else {
        if (anyPicking) ImGui::BeginDisabled();
        if (ImGui::Button(("Pick##" + key).c_str(), ImVec2(btnW, 0))) {
            m_picker->active          = true;
            m_picker->pickAttachment  = false;
            m_picker->pickAnyInstance = false;
            m_picker->prop            = prop;
            m_picker->constraint      = inst.get();
            m_picker->onPick = [inst, propStr = std::string(prop),
                                 nameRefPtr = &nameRef, hist = m_history]
                               (std::shared_ptr<Instance> cube) {
                std::string before = *nameRefPtr;
                std::string after  = cube->getWorkspaceRelativePath();
                YAML::Node n; n = after;
                inst->setProperty(propStr, n);
                if (hist && before != after)
                    hist->record(std::make_unique<SetConstraintCubeNameCommand>(
                        inst, propStr, before, after));
            };
        }
        if (anyPicking) ImGui::EndDisabled();
    }
}

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

void PropertiesPanel::drawConstraintAttachmentRef(const char* label, std::string& nameRef,
                                                   const char* prop, const std::string& cubeName,
                                                   const std::shared_ptr<Instance>& inst)
{
    static std::unordered_map<std::string, std::string> s_before;
    std::string key = std::string(prop) + "_" + inst->Name;

    bool isPickingThis = m_picker && m_picker->active
                      && m_picker->constraint == inst.get()
                      && m_picker->prop == prop;
    bool anyPicking    = m_picker && m_picker->active;

    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    float btnW  = 46.0f;
    float space = ImGui::GetStyle().ItemSpacing.x;
    float fieldW = ImGui::GetContentRegionAvail().x - btnW - space;
    if (fieldW < 60.0f) fieldW = 60.0f;
    ImGui::SetNextItemWidth(fieldW);

    char buf[512] = {};
    strncpy(buf, nameRef.c_str(), sizeof(buf) - 1);
    std::string inputId = "##attref_" + key;
    ImGui::InputText(inputId.c_str(), buf, sizeof(buf));
    if (ImGui::IsItemActivated()) s_before[key] = nameRef;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string after(buf);
        std::string before = s_before[key];
        // setProperty 経由で weak_ptr のリセットと registerIfReady() の再解決を走らせる
        YAML::Node n; n = after;
        inst->setProperty(prop, n);
        if (before != after && m_history)
            m_history->record(std::make_unique<SetConstraintCubeNameCommand>(
                inst, prop, before, after));
    }

    ImGui::SameLine();

    if (isPickingThis) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button(("Cancel##pick_" + key).c_str(), ImVec2(btnW, 0)))
            m_picker->active = false;
        ImGui::PopStyleColor();
    } else {
        if (anyPicking) ImGui::BeginDisabled();
        if (ImGui::Button(("Pick##" + key).c_str(), ImVec2(btnW, 0))) {
            m_picker->active          = true;
            m_picker->pickAttachment  = true;
            m_picker->pickAnyInstance = false;
            m_picker->prop            = prop;
            m_picker->constraint      = inst.get();
            m_picker->onPick = [inst, propStr = std::string(prop), cubeName,
                                 nameRefPtr = &nameRef, hist = m_history]
                               (std::shared_ptr<Instance> att) {
                // Attachment のパスは「最寄りの BaseCube 祖先」相対で保存する
                //（解決側 Attachment::findUnder(cubeX, path) と対になる形式）
                Instance* anchorCube = nullptr;
                for (auto p = att->Parent.lock(); p; p = p->Parent.lock())
                    if (p->IsA("BaseCube")) { anchorCube = p.get(); break; }
                if (!anchorCube) {
                    RCBN_WARN(propStr << ": Attachment \"" << att->Name
                              << "\" は BaseCube の配下に無いため指定できません");
                    return;
                }
                // 対応する Cube0/Cube1 と違うキューブ配下なら解決できないので知らせる（設定自体は行う）
                if (!cubeName.empty() && cubeName != anchorCube->getWorkspaceRelativePath()) {
                    RCBN_WARN(propStr << ": Attachment \"" << att->Name << "\" は \""
                              << cubeName << "\" ではなく \"" << anchorCube->getWorkspaceRelativePath()
                              << "\" の配下にあります。対応する Cube 参照も合わせてください");
                }
                std::string before = *nameRefPtr;
                std::string after  = att->getPathUpTo(anchorCube);
                YAML::Node n; n = after;
                inst->setProperty(propStr, n);
                if (hist && before != after)
                    hist->record(std::make_unique<SetConstraintCubeNameCommand>(
                        inst, propStr, before, after));
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

    Instance* inst = selectedInstance ? *selectedInstance : nullptr;
    if (selectedInstances && selectedInstances->size() > 1) {
        renderMultiInspector(*selectedInstances, m_history);
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
        ImGui::End();
        return;
    }

    if (m_picker && m_picker->active) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
        ImGui::TextUnformatted(m_picker->pickAnyInstance
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

    static std::string s_nameBefore;
    char nameBuf[256] = {};
    strncpy(nameBuf, inst->Name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        inst->renameTo(std::string(nameBuf));
    }
    if (ImGui::IsItemActivated()) s_nameBefore = inst->Name;
    if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
        std::string after = inst->Name;
        if (s_nameBefore != after) {
            m_history->record(std::make_unique<RenameInstanceCommand>(
                inst->shared_from_this(), s_nameBefore, after));
        }
    }

    // ---- Spatial (Position / Size) ----
    if (inst->IsA("Spatial")) {
        Spatial* s = static_cast<Spatial*>(inst);
        auto spSp = std::static_pointer_cast<Spatial>(inst->shared_from_this());

        ImGui::SeparatorText("Transform");

        ImGui::Text("Position");
        ImGui::SameLine(80.0f);
        drawVec3Field("Position", s->Position, 0.05f, -1e9f, 1e9f, spSp, "Position", m_history);

        ImGui::Text("Size");
        ImGui::SameLine(80.0f);
        drawVec3Field("Size", s->Size, 0.05f, 0.01f, 1000.0f, spSp, "Size", m_history);

        // Rotation (Euler 角, 度数)
        ImGui::Text("Rotation");
        ImGui::SameLine(80.0f);
        {
            // before は実 Quaternion を保存（Euler 往復変換のロスを避ける）
            static std::unordered_map<std::string, Quaternion> s_rotBefore;
            Vector3 euler = s->cframe.Rotation.toEuler();
            float rot[3] = { euler.x, euler.y, euler.z };
            float rotW = ImGui::GetContentRegionAvail().x;
            if (rotW < 60.0f) rotW = 60.0f;
            ImGui::SetNextItemWidth(rotW);
            ImGui::PushID("Rotation");
            bool rotChanged = ImGui::DragFloat3("##rot", rot, 1.0f, -360.0f, 360.0f, "%.1f");
            if (ImGui::IsItemActivated()) s_rotBefore["rot"] = s->cframe.Rotation;
            if (rotChanged) s->cframe.Rotation = Quaternion::fromEuler(Vector3(rot[0], rot[1], rot[2]));
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                Quaternion qBefore = s_rotBefore["rot"];
                Quaternion qAfter  = s->cframe.Rotation;  // 適用済みの実値
                auto sSp = std::static_pointer_cast<Spatial>(inst->shared_from_this());
                m_history->record(std::make_unique<SetRotationCommand>(sSp, qBefore, qAfter));
            }
            ImGui::PopID();
        }

        // CFrame (読み取り専用)
        ImGui::Text("CFrame");
        ImGui::SameLine(80.0f);
        {
            Vector3 euler = s->cframe.Rotation.toEuler();
            ImGui::TextDisabled("pos(%.2f, %.2f, %.2f)  rot(%.1f, %.1f, %.1f)",
                s->Position.x, s->Position.y, s->Position.z,
                euler.x, euler.y, euler.z);
        }
    }

    // ---- BaseCube (Color / Anchored / CanCollide / Material、スキーマ駆動) ----
    if (inst->IsA("BaseCube")) {
        renderSchemaInspector(inst, "BaseCube", m_history);
    }

    // ---- MeshCube ----
    if (inst->getClassName() == "MeshCube") {
        MeshCube* mc = static_cast<MeshCube*>(inst);
        ImGui::SeparatorText("MeshCube");
        ImGui::LabelText("MeshFile", "%s", mc->MeshFile.empty() ? "(none)" : mc->MeshFile.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##meshcube").c_str())) {
            std::string path = getPlatform().openFileDialog({{"GLB (*.glb)", "*.glb"}});
            if (!path.empty()) {
                YAML::Node node; node = path;
                mc->setProperty("MeshFile", node);
            }
        }

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

    // ---- LiquidCube（Density、スキーマ駆動） ----
    if (inst->getClassName() == "LiquidCube") {
        ImGui::SeparatorText("LiquidCube");
        renderSchemaInspector(inst, "LiquidCube", m_history);
    }

    // ---- Sun（Angle、スキーマ駆動） ----
    if (inst->getClassName() == "Sun") {
        ImGui::SeparatorText("Sun");
        renderSchemaInspector(inst, "Sun", m_history);
    }

    // ---- Sound ----
    if (inst->getClassName() == "FileRef") {
        FileRef* fr = static_cast<FileRef*>(inst);
        ImGui::SeparatorText("FileRef");
        ImGui::LabelText("Path", "%s", fr->Path.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##fileref").c_str())) {
            std::string path = getPlatform().openFileDialog({{"All files (*.*)", "*.*"}});
            if (!path.empty()) fr->Path = path;
        }
    }

    if (inst->getClassName() == "Sound") {
        Sound* snd = static_cast<Sound*>(inst);
        auto sndSp = std::static_pointer_cast<Sound>(inst->shared_from_this());
        ImGui::SeparatorText("Sound");
        ImGui::LabelText("ContentPath", "%s", snd->getContentPath().c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##sound").c_str())) {
            std::string path = getPlatform().openFileDialog({{"Audio (*.mp3;*.wav;*.ogg)", "*.mp3;*.wav;*.ogg"}});
            if (!path.empty()) {
                YAML::Node node; node = path;
                snd->setProperty("ContentPath", node);
            }
        }

        // AutoPlay with undo
        {
            bool prev = snd->autoPlay;
            if (ImGui::Checkbox("AutoPlay", &snd->autoPlay) && m_history && snd->autoPlay != prev)
                m_history->record(std::make_unique<SetSoundBoolCommand>(sndSp, "AutoPlay", prev, snd->autoPlay));
        }

        // Looped with undo
        {
            bool looping = snd->isLooping();
            bool prev = looping;
            if (ImGui::Checkbox("Looped", &looping)) {
                snd->setLooping(looping);
                if (m_history)
                    m_history->record(std::make_unique<SetSoundBoolCommand>(sndSp, "Looped", prev, looping));
            }
        }

        // Volume with undo（編集開始時の値を記録し、確定時にコマンド化）
        {
            static float volBefore = 0.0f;
            float vol = snd->getVolume();
            ImGui::SliderFloat("Volume", &vol, 0.0f, 1.0f);
            if (ImGui::IsItemActivated()) volBefore = snd->getVolume();
            snd->setVolume(vol);
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history && vol != volBefore)
                m_history->record(std::make_unique<SetSoundFloatCommand>(sndSp, "Volume", volBefore, vol));
        }

        // Speed with undo
        {
            static float spdBefore = 1.0f;
            float spd = snd->getSpeed();
            ImGui::SliderFloat("Speed", &spd, 0.25f, 4.0f);
            if (ImGui::IsItemActivated()) spdBefore = snd->getSpeed();
            snd->setSpeed(spd);
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history && spd != spdBefore)
                m_history->record(std::make_unique<SetSoundFloatCommand>(sndSp, "Speed", spdBefore, spd));
        }

        // PreservePitch with undo
        {
            bool pp = snd->getPreservePitch();
            bool prev = pp;
            if (ImGui::Checkbox("PreservePitch", &pp)) {
                snd->setPreservePitch(pp);
                if (m_history)
                    m_history->record(std::make_unique<SetSoundBoolCommand>(sndSp, "PreservePitch", prev, pp));
            }
        }

        // 再生時間スクラバ（ライブ値のため Undo 対象外）
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
    if (inst->getClassName() == "Script") {
        Script* sc = static_cast<Script*>(inst);
        auto scSp = std::static_pointer_cast<Script>(inst->shared_from_this());
        ImGui::SeparatorText("Script");
        ImGui::LabelText("Source", "%s", sc->Path.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##script").c_str())) {
            std::string path = getPlatform().openFileDialog({{"Luau Script (*.luau;*.lua)", "*.luau;*.lua"}});
            if (!path.empty()) {
                YAML::Node node; node = path;
                sc->setProperty("Path", node);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::OpenExternalEditor)) && !sc->Path.empty()) {
            getPlatform().revealInFileManager(sc->Path);
        }

        {
            bool before = sc->Enabled;
            bool value  = sc->Enabled;
            if (ImGui::Checkbox("Enabled", &value)) {
                sc->Enabled = value;
                if (m_history) m_history->record(std::make_unique<SetScriptBoolCommand>(scSp, "Enabled", before, value));
            }
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
                    dcl->Mode = newMode;
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
                if (centerChanged) dcl->UVCenter = Vector2(center[0], center[1]);
                if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                    m_history->record(std::make_unique<SetDecalUVCommand>(
                        dclSp, s_dclUVCenterBefore, s_dclUVRadiusBefore, dcl->UVCenter, dcl->UVRadius));
                }

                float radius = dcl->UVRadius;
                bool radiusChanged = ImGui::DragFloat("UVRadius", &radius, 0.005f, 0.01f, 1.0f, "%.3f");
                if (ImGui::IsItemActivated()) { s_dclUVCenterBefore = dcl->UVCenter; s_dclUVRadiusBefore = dcl->UVRadius; }
                if (radiusChanged) dcl->UVRadius = radius;
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

        // Color with undo
        {
            static Color4 s_dclColorBefore;
            float col[4] = { dcl->Color.r, dcl->Color.g, dcl->Color.b, dcl->Color.a };
            if (ImGui::IsItemActivated()) s_dclColorBefore = dcl->Color;
            bool colorChanged = ImGui::ColorEdit4("Color##decal", col);
            if (ImGui::IsItemActivated()) s_dclColorBefore = dcl->Color;
            if (colorChanged) dcl->Color = Color4(col[0], col[1], col[2], col[3]);
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                Color4 after(col[0], col[1], col[2], col[3]);
                m_history->record(std::make_unique<SetDecalColorCommand>(dclSp, s_dclColorBefore, after));
            }
        }

        // Texture with undo
        ImGui::LabelText("Texture", "%s", dcl->texturePath.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##decal").c_str())) {
            std::string path = getPlatform().openFileDialog({{"Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga"}});
            if (!path.empty()) {
                std::string oldPath = dcl->texturePath;
                unsigned int oldID  = dcl->TextureID;
                YAML::Node node; node = path;
                dcl->setProperty("Texture", node);
                if (m_history)
                    m_history->record(std::make_unique<SetDecalTextureCommand>(
                        dclSp, oldPath, oldID, dcl->texturePath, dcl->TextureID));
            }
        }
    }

    // ---- Texture ----
    if (inst->getClassName() == "Texture") {
        Texture* tx = static_cast<Texture*>(inst);
        auto txSp = std::static_pointer_cast<Texture>(inst->shared_from_this());
        ImGui::SeparatorText("Texture");

        // Face combo with undo
        {
            static const char* faceItems[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };
            int faceIdx = static_cast<int>(tx->face);
            if (ImGui::Combo("Face##tex", &faceIdx, faceItems, 6)) {
                Face newFace = static_cast<Face>(faceIdx);
                if (newFace != tx->face) {
                    Face oldFace = tx->face;
                    tx->setFace(newFace);
                    if (m_history)
                        m_history->record(std::make_unique<SetTextureFaceCommand>(txSp, oldFace, newFace));
                }
            }
        }

        // Color with undo
        {
            static Color4 s_txColorBefore;
            float col[4] = { tx->Color.r, tx->Color.g, tx->Color.b, tx->Color.a };
            if (ImGui::IsItemActivated()) s_txColorBefore = tx->Color;
            bool colorChanged = ImGui::ColorEdit4("Color##tex", col);
            if (ImGui::IsItemActivated()) s_txColorBefore = tx->Color;
            if (colorChanged) tx->Color = Color4(col[0], col[1], col[2], col[3]);
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                Color4 after(col[0], col[1], col[2], col[3]);
                m_history->record(std::make_unique<SetTextureColorCommand>(txSp, s_txColorBefore, after));
            }
        }

        // StudsPerTileU / V with undo
        {
            static float s_studsUBefore, s_studsVBefore;
            float studsU = tx->StudsPerTileU;
            float studsV = tx->StudsPerTileV;
            if (ImGui::DragFloat("StudsPerTileU", &studsU, 0.1f, 0.01f, 100.0f)) {
                if (ImGui::IsItemActivated()) { s_studsUBefore = tx->StudsPerTileU; s_studsVBefore = tx->StudsPerTileV; }
                tx->StudsPerTileU = studsU;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetTextureStudsCommand>(
                    txSp, s_studsUBefore, s_studsVBefore, tx->StudsPerTileU, tx->StudsPerTileV));
            }
            if (ImGui::DragFloat("StudsPerTileV", &studsV, 0.1f, 0.01f, 100.0f)) {
                if (ImGui::IsItemActivated()) { s_studsUBefore = tx->StudsPerTileU; s_studsVBefore = tx->StudsPerTileV; }
                tx->StudsPerTileV = studsV;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetTextureStudsCommand>(
                    txSp, s_studsUBefore, s_studsVBefore, tx->StudsPerTileU, tx->StudsPerTileV));
            }
        }

        // Texture path with undo
        ImGui::LabelText("Texture##texpath", "%s", tx->texturePath.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##tex").c_str())) {
            std::string path = getPlatform().openFileDialog({{"Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga"}});
            if (!path.empty()) {
                std::string oldPath = tx->texturePath;
                unsigned int oldID  = tx->TextureID;
                YAML::Node node; node = path;
                tx->setProperty("Texture", node);
                if (m_history)
                    m_history->record(std::make_unique<SetTextureTextureCommand>(
                        txSp, oldPath, oldID, tx->texturePath, tx->TextureID));
            }
        }
    }

    // ---- System ----
    if (inst->getClassName() == "System") {
        System* sys = static_cast<System*>(inst);
        auto sysSp = std::static_pointer_cast<System>(inst->shared_from_this());

        // BaseResolutionは安全マージンではないため、Safety Limits欄の外（上）に表示する。
        ImGui::SeparatorText("System");
        renderSchemaInspector(inst, "System", m_history);

        ImGui::SeparatorText("System (Safety Limits)");

        {
            static int s_before;
            ImGui::DragInt("MaxClonesPerFrame", &sys->MaxClonesPerFrame, 1.0f, 0, 1000000);
            if (ImGui::IsItemActivated()) s_before = sys->MaxClonesPerFrame;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
                m_history->record(std::make_unique<SetSystemIntCommand>(sysSp, "MaxClonesPerFrame", s_before, sys->MaxClonesPerFrame));
        }
        {
            static int s_before;
            ImGui::DragInt("MaxRestartsPerFrame", &sys->MaxRestartsPerFrame, 1.0f, 0, 1000000);
            if (ImGui::IsItemActivated()) s_before = sys->MaxRestartsPerFrame;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
                m_history->record(std::make_unique<SetSystemIntCommand>(sysSp, "MaxRestartsPerFrame", s_before, sys->MaxRestartsPerFrame));
        }
        {
            static int s_before;
            ImGui::DragInt("MaxTasksPerFrame", &sys->MaxTasksPerFrame, 1.0f, 0, 1000000);
            if (ImGui::IsItemActivated()) s_before = sys->MaxTasksPerFrame;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
                m_history->record(std::make_unique<SetSystemIntCommand>(sysSp, "MaxTasksPerFrame", s_before, sys->MaxTasksPerFrame));
        }
        {
            static float s_before;
            ImGui::DragFloat("ScriptLoopTimeoutSeconds", &sys->ScriptLoopTimeoutSeconds, 0.05f, 0.0f, 60.0f, "%.2f");
            if (ImGui::IsItemActivated()) s_before = sys->ScriptLoopTimeoutSeconds;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
                m_history->record(std::make_unique<SetSystemFloatCommand>(sysSp, "ScriptLoopTimeoutSeconds", s_before, sys->ScriptLoopTimeoutSeconds));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 or below disables the loop timeout check");
    }

    // ---- User ----
    if (inst->getClassName() == "User") {
        User* usr = static_cast<User*>(inst);
        auto usrSp = std::static_pointer_cast<User>(inst->shared_from_this());
        ImGui::SeparatorText("User");

        // ControlMode (combo)
        {
            static const char* controlModes[] = { "Free", "Character", "Program" };
            int modeIdx = (usr->controlMode == User::ControlMode::Free) ? 0
                        : (usr->controlMode == User::ControlMode::Character) ? 1 : 2;
            if (ImGui::Combo("ControlMode", &modeIdx, controlModes, 3)) {
                usr->controlMode = (modeIdx == 0) ? User::ControlMode::Free
                                  : (modeIdx == 1) ? User::ControlMode::Character
                                                    : User::ControlMode::Program;
            }
        }

        // Character mode parameters
        ImGui::DragFloat("Speed", &usr->speed, 0.01f, 0.0f, 10.0f, "%.3f");
        ImGui::DragFloat("RotationSpeed", &usr->rotationSpeed, 0.01f, 0.0f, 10.0f, "%.3f");
        ImGui::DragFloat("MouseRotationSpeed", &usr->mouseRotationSpeed, 0.01f, 0.0f, 2.0f, "%.3f");
        ImGui::DragFloat("CameraDistance", &usr->cameraDistance, 0.1f, 1.0f, 50.0f, "%.2f");
        ImGui::DragFloat("ZoomSpeed", &usr->zoomSpeed, 0.01f, 0.0f, 1.0f, "%.3f");
        ImGui::DragFloat("MouseZoomSpeed", &usr->mouseZoomSpeed, 0.1f, 0.0f, 10.0f, "%.2f");

        // Current slot index (read-only)
        ImGui::LabelText("CurrentSlotIndex", "%d", usr->currentSlotIndex);

        // Inventory (reference)
        if (usr->Inventory) {
            ImGui::LabelText("Inventory", "%s", usr->Inventory->Name.c_str());
        }

        // Tool slots (read-only)
        ImGui::SeparatorText("Tool Slots");
        for (int i = 0; i < 10; i++) {
            std::string slotLabel = "Slot " + std::to_string(i);
            ImGui::LabelText(slotLabel.c_str(), "%s", usr->Slots[i] ? usr->Slots[i]->Name.c_str() : "(empty)");
        }
    }

    // ---- Tool ----
    if (inst->getClassName() == "Tool") {
        Tool* tool = static_cast<Tool*>(inst);
        auto toolSp = std::static_pointer_cast<Tool>(inst->shared_from_this());
        ImGui::SeparatorText("Tool");

        // Equipped flag (ReadOnly: ゲームプレイ中にUser側で制御されるため編集不可)
        ImGui::LabelText("Equipped", "%s", tool->Equipped ? "true" : "false");

        // Hand (combo)
        {
            static const char* handModes[] = { "Right", "Left", "Both" };
            int handIdx = static_cast<int>(tool->Hand);
            if (ImGui::Combo("Hand", &handIdx, handModes, 3)) {
                tool->Hand = static_cast<Tool::ToolHand>(handIdx);
            }
        }

        // Handle reference（制約と同じ Pick 機構で指定。Viewport / ヒエラルキーから選択可）
        drawConstraintCubeRef("Handle", tool->m_handleName, "Handle", toolSp);
        if (tool->Handle) {
            ImGui::TextDisabled("\xe2\x86\x92 %s", tool->Handle->Name.c_str());
        } else if (!tool->m_handleName.empty()) {
            ImGui::TextDisabled("\xe2\x86\x92 %s", Loc::t(Loc::LocKey::ToolUnresolved));
        }
    }

    // ---- Lighting（スキーマ駆動） ----
    if (inst->getClassName() == "Lighting") {
        ImGui::SeparatorText("Lighting");
        renderSchemaInspector(inst, "Lighting", m_history);
    }

    // ---- LightSource（PointLight / SpotLight、スキーマ駆動） ----
    if (inst->IsA("LightSource")) {
        ImGui::SeparatorText("Light");
        renderSchemaInspector(inst, "LightSource", m_history);
    }
    if (inst->getClassName() == "SpotLight") {
        ImGui::SeparatorText("SpotLight");
        renderSchemaInspector(inst, "SpotLight", m_history);
    }

    // ---- ParticleEmitter（スキーマ駆動） ----
    if (inst->getClassName() == "ParticleEmitter") {
        ImGui::SeparatorText("ParticleEmitter");
        renderSchemaInspector(inst, "ParticleEmitter", m_history);
    }

    // ---- Weather（スキーマ駆動。文字列プロパティ(ClearAmbientPath等)は上のInputTextで直接編集可能だが、
    //      音声ファイル選択の利便性のため参照ボタンも添える） ----
    if (inst->getClassName() == "Weather") {
        ImGui::SeparatorText("Weather");
        renderSchemaInspector(inst, "Weather", m_history);

        auto browseAmbient = [&](const char* propName, const char* idSuffix) {
            std::string btnLabel = std::string(propName) + " " + Loc::t(Loc::LocKey::Browse) + "##" + idSuffix;
            if (ImGui::Button(btnLabel.c_str())) {
                std::string path = getPlatform().openFileDialog({{"Audio (*.mp3;*.wav;*.ogg)", "*.mp3;*.wav;*.ogg"}});
                if (!path.empty()) {
                    YAML::Node node; node = path;
                    inst->setProperty(propName, node);
                }
            }
        };
        browseAmbient("ClearAmbientPath", "weatherclear");
        browseAmbient("RainAmbientPath",  "weatherrain");
        browseAmbient("SnowAmbientPath",  "weathersnow");
    }

    // ---- PostEffect ----
    if (inst->getClassName() == "PostEffect") {
        PostEffect* pe = static_cast<PostEffect*>(inst);
        auto peSp = std::static_pointer_cast<PostEffect>(inst->shared_from_this());
        ImGui::SeparatorText("PostEffect");

        // Enabled
        {
            bool before = pe->Enabled;
            bool value  = pe->Enabled;
            if (ImGui::Checkbox("Enabled##posteffect", &value)) {
                pe->Enabled = value;
                if (m_history) m_history->record(std::make_unique<SetPostEffectBoolCommand>(peSp, "Enabled", before, value));
            }
        }

        // Type (combo)
        static const char* peTypes[] = { "None", "CRT", "Posterization", "Pixelize", "Saturation", "VHS", "ChromaticAberration" };
        {
            int typeIdx = static_cast<int>(pe->Type);
            int beforeIdx = typeIdx;
            if (ImGui::Combo("Type", &typeIdx, peTypes, 7)) {
                PostEffectKind before = pe->Type;
                pe->Type = static_cast<PostEffectKind>(typeIdx);
                if (m_history) m_history->record(std::make_unique<SetPostEffectTypeCommand>(peSp, before, pe->Type));
            }
            (void)beforeIdx;
        }

        // ZIndex with undo
        {
            static int s_zBefore;
            int zIndex = pe->ZIndex;
            bool changed = ImGui::DragInt("ZIndex", &zIndex);
            if (ImGui::IsItemActivated()) s_zBefore = pe->ZIndex;
            if (changed) pe->ZIndex = zIndex;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetPostEffectIntCommand>(peSp, "ZIndex", s_zBefore, pe->ZIndex));
            }
        }

        // Intensity with undo
        {
            static float s_intensityBefore;
            bool changed = ImGui::DragFloat("Intensity", &pe->Intensity, 0.01f, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemActivated()) s_intensityBefore = pe->Intensity;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetPostEffectFloatCommand>(peSp, "Intensity", s_intensityBefore, pe->Intensity));
            }
        }

        // Param1 / Param2: Type に応じてラベルと範囲を切替
        const char* param1Label = "Param1";
        const char* param2Label = nullptr;
        float param1Min = 1.0f, param1Max = 256.0f, param1Speed = 0.1f;
        switch (pe->Type) {
            case PostEffectKind::CRT:                param1Label = "ScanlineCount"; param2Label = "CurveAmount"; break;
            case PostEffectKind::Posterization:      param1Label = "Levels";        break;
            case PostEffectKind::Pixelize:            param1Label = "PixelSize";     break;
            case PostEffectKind::Saturation:          param1Label = "Saturation";    param1Min = -1.0f; param1Max = 2.0f; param1Speed = 0.01f; break;
            case PostEffectKind::VHS:                 param1Label = "NoiseAmount";   param1Min = 0.0f;  param1Max = 1.0f; param1Speed = 0.01f; break;
            case PostEffectKind::ChromaticAberration: param1Label = "Offset";        param1Min = 0.0f;  param1Max = 1.0f; param1Speed = 0.01f; break;
            default: break;
        }

        {
            static float s_param1Before;
            bool changed = ImGui::DragFloat(param1Label, &pe->Param1, param1Speed, param1Min, param1Max, "%.2f");
            if (ImGui::IsItemActivated()) s_param1Before = pe->Param1;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetPostEffectFloatCommand>(peSp, "Param1", s_param1Before, pe->Param1));
            }
        }

        if (param2Label) {
            static float s_param2Before;
            bool changed = ImGui::DragFloat(param2Label, &pe->Param2, 0.01f, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemActivated()) s_param2Before = pe->Param2;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetPostEffectFloatCommand>(peSp, "Param2", s_param2Before, pe->Param2));
            }
        }
    }

    // ---- Terrain ----
    if (inst->getClassName() == "Terrain") {
        Terrain* terrain = static_cast<Terrain*>(inst);
        auto terrSp = std::static_pointer_cast<Terrain>(inst->shared_from_this());
        ImGui::SeparatorText("Terrain");

        {
            bool before = terrain->Enabled;
            bool value  = terrain->Enabled;
            if (ImGui::Checkbox("Enabled##terrain", &value)) {
                terrain->Enabled = value;
                if (m_history) m_history->record(std::make_unique<SetTerrainBoolCommand>(terrSp, "Enabled", before, value));
            }
        }

        // データ保存先ディレクトリ（リージョンファイルの置き場所）— フォルダ参照
        ImGui::LabelText("DataPath", "%s", terrain->DataPath.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##terraindp").c_str())) {
            std::string folder = getPlatform().openFolderDialog();
            if (!folder.empty()) {
                std::string before = terrain->DataPath;
                YAML::Node node; node = folder;
                terrain->setProperty("DataPath", node);
                if (m_history) m_history->record(std::make_unique<SetTerrainStringCommand>(terrSp, "DataPath", before, terrain->DataPath));
            }
        }

        // 生成設定（Seed / Flat）
        ImGui::Separator();
        {
            static int s_before;
            ImGui::InputInt("Seed##terrain", &terrain->Seed);
            if (ImGui::IsItemActivated()) s_before = terrain->Seed;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
                m_history->record(std::make_unique<SetTerrainIntCommand>(terrSp, "Seed", s_before, terrain->Seed));
        }
        ImGui::SameLine();
        if (ImGui::Button(locId(Loc::LocKey::TerrainRandomize, "##terrainseed").c_str())) {
            int before = terrain->Seed;
            std::random_device rd;
            terrain->Seed = static_cast<int>(rd());
            if (m_history) m_history->record(std::make_unique<SetTerrainIntCommand>(terrSp, "Seed", before, terrain->Seed));
        }
        {
            bool before = terrain->Flat;
            bool value  = terrain->Flat;
            if (ImGui::Checkbox(locId(Loc::LocKey::TerrainFlatCheckbox, "##terrain").c_str(), &value)) {
                terrain->Flat = value;
                if (m_history) m_history->record(std::make_unique<SetTerrainBoolCommand>(terrSp, "Flat", before, value));
            }
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

    // ---- Rope ----
    if (inst->getClassName() == "Rope") {
        Rope* rope = static_cast<Rope*>(inst);
        auto ropeSp = std::static_pointer_cast<Rope>(inst->shared_from_this());
        ImGui::SeparatorText("Rope");

        drawConstraintCubeRef("Cube0", rope->m_cube0Name, "Cube0", ropeSp);
        drawConstraintCubeRef("Cube1", rope->m_cube1Name, "Cube1", ropeSp);
        drawConstraintAttachmentRef("Attachment0", rope->m_attachment0Name, "Attachment0", rope->m_cube0Name, ropeSp);
        drawConstraintAttachmentRef("Attachment1", rope->m_attachment1Name, "Attachment1", rope->m_cube1Name, ropeSp);

        static float s_rf;
        { ImGui::DragFloat("MaxDistance", &rope->MaxDistance, 0.1f, 0.0f, 1e6f);
          if (ImGui::IsItemActivated()) s_rf = rope->MaxDistance;
          if (ImGui::IsItemDeactivatedAfterEdit()) {
              rope->setMaxDistance(rope->MaxDistance);
              if (m_history) m_history->record(std::make_unique<SetRopeFloatCommand>(ropeSp, "MaxDistance", s_rf, rope->MaxDistance)); } }
        { ImGui::DragFloat("Stiffness",   &rope->Stiffness,  1.0f, 0.0f, 1e6f);
          if (ImGui::IsItemActivated()) s_rf = rope->Stiffness;
          if (ImGui::IsItemDeactivatedAfterEdit()) {
              rope->setStiffness(rope->Stiffness);
              if (m_history) m_history->record(std::make_unique<SetRopeFloatCommand>(ropeSp, "Stiffness", s_rf, rope->Stiffness)); } }
        { ImGui::DragFloat("Damping",     &rope->Damping,    0.1f, 0.0f, 1e6f);
          if (ImGui::IsItemActivated()) s_rf = rope->Damping;
          if (ImGui::IsItemDeactivatedAfterEdit()) {
              rope->setDamping(rope->Damping);
              if (m_history) m_history->record(std::make_unique<SetRopeFloatCommand>(ropeSp, "Damping", s_rf, rope->Damping)); } }
        { static Color4 s_rc;
          float col[4] = { rope->Color.r, rope->Color.g, rope->Color.b, rope->Color.a };
          if (ImGui::ColorEdit4("Color", col)) { rope->Color = {col[0], col[1], col[2], col[3]}; }
          if (ImGui::IsItemActivated()) s_rc = rope->Color;
          if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
              m_history->record(std::make_unique<SetRopeColorCommand>(ropeSp, s_rc, rope->Color)); }
        { static float s_rlw;
          if (ImGui::DragFloat("LineWidth", &rope->LineWidth, 0.1f, 0.5f, 16.0f)){}
          if (ImGui::IsItemActivated()) s_rlw = rope->LineWidth;
          if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
              m_history->record(std::make_unique<SetRopeLineWidthCommand>(ropeSp, s_rlw, rope->LineWidth)); }
    }

    // ---- Rod ----
    if (inst->getClassName() == "Rod") {
        Rod* rod = static_cast<Rod*>(inst);
        auto rodSp = std::static_pointer_cast<Rod>(inst->shared_from_this());
        ImGui::SeparatorText("Rod");
        drawConstraintCubeRef("Cube0", rod->m_cube0Name, "Cube0", rodSp);
        drawConstraintCubeRef("Cube1", rod->m_cube1Name, "Cube1", rodSp);
        drawConstraintAttachmentRef("Attachment0", rod->m_attachment0Name, "Attachment0", rod->m_cube0Name, rodSp);
        drawConstraintAttachmentRef("Attachment1", rod->m_attachment1Name, "Attachment1", rod->m_cube1Name, rodSp);
        { static Color4 s_rdc;
          float col[4] = { rod->Color.r, rod->Color.g, rod->Color.b, rod->Color.a };
          if (ImGui::ColorEdit4("Color", col)) { rod->Color = {col[0], col[1], col[2], col[3]}; }
          if (ImGui::IsItemActivated()) s_rdc = rod->Color;
          if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
              m_history->record(std::make_unique<SetRodColorCommand>(rodSp, s_rdc, rod->Color)); }
        { static float s_rdlw;
          if (ImGui::DragFloat("LineWidth", &rod->LineWidth, 0.1f, 0.5f, 16.0f)){}
          if (ImGui::IsItemActivated()) s_rdlw = rod->LineWidth;
          if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
              m_history->record(std::make_unique<SetRodLineWidthCommand>(rodSp, s_rdlw, rod->LineWidth)); }
    }

    // ---- BallSocket ----
    if (inst->getClassName() == "BallSocket") {
        BallSocket* bs = static_cast<BallSocket*>(inst);
        auto bsSp = std::static_pointer_cast<BallSocket>(inst->shared_from_this());
        ImGui::SeparatorText("BallSocket");
        drawConstraintCubeRef("Cube0", bs->m_cube0Name, "Cube0", bsSp);
        drawConstraintCubeRef("Cube1", bs->m_cube1Name, "Cube1", bsSp);
        drawConstraintAttachmentRef("Attachment0", bs->m_attachment0Name, "Attachment0", bs->m_cube0Name, bsSp);
        drawConstraintAttachmentRef("Attachment1", bs->m_attachment1Name, "Attachment1", bs->m_cube1Name, bsSp);
    }

    // ---- NoCollision ----
    if (inst->getClassName() == "NoCollision") {
        NoCollision* nc = static_cast<NoCollision*>(inst);
        auto ncSp = std::static_pointer_cast<NoCollision>(inst->shared_from_this());
        ImGui::SeparatorText("NoCollision");
        drawConstraintCubeRef("Cube0", nc->m_cube0Name, "Cube0", ncSp);
        drawConstraintCubeRef("Cube1", nc->m_cube1Name, "Cube1", ncSp);
    }

    // ---- Weld ----
    if (inst->getClassName() == "Weld") {
        Weld* weld = static_cast<Weld*>(inst);
        auto weldSp = std::static_pointer_cast<Weld>(inst->shared_from_this());
        ImGui::SeparatorText("Weld");
        drawConstraintCubeRef("Cube0", weld->m_cube0Name, "Cube0", weldSp);
        drawConstraintCubeRef("Cube1", weld->m_cube1Name, "Cube1", weldSp);
    }

    // ---- Motor ----
    if (inst->getClassName() == "Motor") {
        Motor* motor = static_cast<Motor*>(inst);
        auto motorSp = std::static_pointer_cast<Motor>(inst->shared_from_this());
        ImGui::SeparatorText("Motor");

        drawConstraintCubeRef("Cube0", motor->m_cube0Name, "Cube0", motorSp);
        drawConstraintCubeRef("Cube1", motor->m_cube1Name, "Cube1", motorSp);
        drawConstraintAttachmentRef("Attachment0", motor->m_attachment0Name, "Attachment0", motor->m_cube0Name, motorSp);
        drawConstraintAttachmentRef("Attachment1", motor->m_attachment1Name, "Attachment1", motor->m_cube1Name, motorSp);

        { static Vector3 s_axisBefore;
          float ax[3] = { motor->Axis.x, motor->Axis.y, motor->Axis.z };
          bool ch = ImGui::DragFloat3("Axis", ax, 0.01f, -1.0f, 1.0f, "%.3f");
          if (ImGui::IsItemActivated()) s_axisBefore = motor->Axis;
          if (ch) motor->Axis = Vector3(ax[0], ax[1], ax[2]);
          if (ImGui::IsItemDeactivatedAfterEdit() && m_history)
              m_history->record(std::make_unique<SetMotorAxisCommand>(motorSp, s_axisBefore, motor->Axis)); }

        static float s_mf;
        { ImGui::DragFloat("DriveVelocity", &motor->DriveVelocity, 0.1f, -1e4f, 1e4f);
          if (ImGui::IsItemActivated()) s_mf = motor->DriveVelocity;
          if (ImGui::IsItemDeactivatedAfterEdit()) {
              motor->setDriveVelocity(motor->DriveVelocity);
              if (m_history) m_history->record(std::make_unique<SetMotorFloatCommand>(motorSp, "DriveVelocity", s_mf, motor->DriveVelocity)); } }
        { ImGui::DragFloat("MaxForce",      &motor->MaxForce,      10.0f, 0.0f, 1e7f);
          if (ImGui::IsItemActivated()) s_mf = motor->MaxForce;
          if (ImGui::IsItemDeactivatedAfterEdit()) {
              motor->setMaxForce(motor->MaxForce);
              if (m_history) m_history->record(std::make_unique<SetMotorFloatCommand>(motorSp, "MaxForce", s_mf, motor->MaxForce)); } }
    }

    // ---- AppImage ----
    if (inst->getClassName() == "AppImage") {
        AppImage* ai = static_cast<AppImage*>(inst);
        ImGui::SeparatorText("AppImage");
        ImGui::LabelText("IconPath", "%s", ai->iconPath.empty() ? "(none)" : ai->iconPath.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##appimage").c_str())) {
            std::string path = getPlatform().openFileDialog({{"Image (*.png;*.jpg;*.bmp;*.ico)", "*.png;*.jpg;*.bmp;*.ico"}});
            if (!path.empty()) {
                YAML::Node node; node = path;
                ai->setProperty("IconPath", node);
            }
        }
    }

    // ---- Force（スキーマ駆動） ----
    if (inst->getClassName() == "Force") {
        ImGui::SeparatorText("Force");
        renderSchemaInspector(inst, "Force", m_history);
    }

    // ---- Humanoid（スキーマ駆動。プロパティ追加はスキーマに1行足すだけ） ----
    if (inst->getClassName() == "Humanoid") {
        ImGui::SeparatorText("Humanoid");
        renderSchemaInspector(inst, "Humanoid", m_history);
    }

    // ---- Seat（Steer/Throttleは着席中エンジンが書き込むLua読取専用値。確認用に表示） ----
    if (inst->getClassName() == "Seat") {
        ImGui::SeparatorText("Seat");
        renderSchemaInspector(inst, "Seat", m_history);
    }

    // ---- ScreenGuiObject ----
    // ---- GUI 一族（スキーマ駆動。基底は IsA ブロック、葉は getClassName ブロックで描画） ----
    if (inst->IsA("ScreenGuiObject")) {
        ImGui::SeparatorText("ScreenGuiObject");
        renderSchemaInspector(inst, "ScreenGuiObject", m_history);
    }
    if (inst->getClassName() == "TextLabel") {
        ImGui::SeparatorText("TextLabel");
        renderSchemaInspector(inst, "TextLabel", m_history);
    }
    if (inst->getClassName() == "TextButton") {
        ImGui::SeparatorText("TextButton");
        renderSchemaInspector(inst, "TextButton", m_history);
    }
    if (inst->IsA("WorldGuiObject")) {
        ImGui::SeparatorText("WorldGuiObject");
        renderSchemaInspector(inst, "WorldGuiObject", m_history);
    }
    if (inst->getClassName() == "SurfaceGui") {
        ImGui::SeparatorText("SurfaceGui");
        renderSchemaInspector(inst, "SurfaceGui", m_history);
    }
    if (inst->getClassName() == "Canvas") {
        ImGui::SeparatorText("Canvas");
        renderSchemaInspector(inst, "Canvas", m_history);
    }
    if (inst->getClassName() == "Highlight") {
        ImGui::SeparatorText("Highlight");
        renderSchemaInspector(inst, "Highlight", m_history);
    }
    if (inst->getClassName() == "BillboardGui") {
        ImGui::SeparatorText("BillboardGui");
        renderSchemaInspector(inst, "BillboardGui", m_history);
    }
    if (inst->getClassName() == "ProximityPrompt") {
        ImGui::SeparatorText("ProximityPrompt");
        renderSchemaInspector(inst, "ProximityPrompt", m_history);
    }
    if (inst->getClassName() == "IntValue") {
        ImGui::SeparatorText("IntValue");
        renderSchemaInspector(inst, "IntValue", m_history);
    }
    if (inst->getClassName() == "BoolValue") {
        ImGui::SeparatorText("BoolValue");
        renderSchemaInspector(inst, "BoolValue", m_history);
    }
    if (inst->getClassName() == "Vector3Value") {
        ImGui::SeparatorText("Vector3Value");
        renderSchemaInspector(inst, "Vector3Value", m_history);
    }
    if (inst->getClassName() == "Color4Value") {
        ImGui::SeparatorText("Color4Value");
        renderSchemaInspector(inst, "Color4Value", m_history);
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
    if (inst->getClassName() == "CFrameValue") {
        ImGui::SeparatorText("CFrameValue");
        auto* cv = static_cast<CFrameValue*>(inst);
        static std::unordered_map<std::string, CFrame> s_cfBefore;
        std::string key = "cfval_" + inst->Name;

        auto applyCFrame = [&](const CFrame& v) {
            YAML::Node n;
            YAML::Node pos; pos.push_back(v.Position.x); pos.push_back(v.Position.y); pos.push_back(v.Position.z);
            YAML::Node rot; rot.push_back(v.Rotation.x); rot.push_back(v.Rotation.y); rot.push_back(v.Rotation.z); rot.push_back(v.Rotation.w);
            n["Position"] = pos; n["Rotation"] = rot;
            inst->setProperty("Value", n);
        };

        ImGui::Text("Position");
        ImGui::SameLine(80.0f);
        {
            float pos[3] = { cv->Value.Position.x, cv->Value.Position.y, cv->Value.Position.z };
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            bool changed = ImGui::DragFloat3("##cfpos", pos, 0.05f);
            if (ImGui::IsItemActivated()) s_cfBefore[key + "_pos"] = cv->Value;
            if (changed) { CFrame v = cv->Value; v.Position = Vector3(pos[0], pos[1], pos[2]); applyCFrame(v); }
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetCFrameValueCommand>(
                    inst->shared_from_this(), s_cfBefore[key + "_pos"], cv->Value));
            }
        }

        ImGui::Text("Rotation");
        ImGui::SameLine(80.0f);
        {
            Vector3 euler = cv->Value.Rotation.toEuler();
            float rot[3] = { euler.x, euler.y, euler.z };
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            bool changed = ImGui::DragFloat3("##cfrot", rot, 1.0f, -360.0f, 360.0f, "%.1f");
            if (ImGui::IsItemActivated()) s_cfBefore[key + "_rot"] = cv->Value;
            if (changed) { CFrame v = cv->Value; v.Rotation = Quaternion::fromEuler(Vector3(rot[0], rot[1], rot[2])); applyCFrame(v); }
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetCFrameValueCommand>(
                    inst->shared_from_this(), s_cfBefore[key + "_rot"], cv->Value));
            }
        }
    }
    if (inst->getClassName() == "QuaternionValue") {
        ImGui::SeparatorText("QuaternionValue");
        auto* qv = static_cast<QuaternionValue*>(inst);
        static std::unordered_map<std::string, Quaternion> s_qBefore;
        std::string key = "qval_" + inst->Name;

        ImGui::Text("Value");
        ImGui::SameLine(80.0f);
        Vector3 euler = qv->Value.toEuler();
        float rot[3] = { euler.x, euler.y, euler.z };
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        bool changed = ImGui::DragFloat3("##qrot", rot, 1.0f, -360.0f, 360.0f, "%.1f");
        if (ImGui::IsItemActivated()) s_qBefore[key] = qv->Value;
        if (changed) {
            Quaternion v = Quaternion::fromEuler(Vector3(rot[0], rot[1], rot[2]));
            YAML::Node n; n.push_back(v.x); n.push_back(v.y); n.push_back(v.z); n.push_back(v.w);
            inst->setProperty("Value", n);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
            m_history->record(std::make_unique<SetQuaternionValueCommand>(
                inst->shared_from_this(), s_qBefore[key], qv->Value));
        }
    }
    if (inst->getClassName() == "ObjectValue") {
        ImGui::SeparatorText("ObjectValue");
        drawObjectValueRef("Value", std::static_pointer_cast<Instance>(inst->shared_from_this()));
    }
    if (inst->getClassName() == "ImageLabel" || inst->getClassName() == "ImageButton") {
        const std::string cn = inst->getClassName();
        ImGui::SeparatorText(cn.c_str());

        // Image: パス表示 + 参照ボタン（Decal/AppImage と同方式）
        const PropertyDesc* imgDesc = nullptr;
        for (const auto& d : PropertyRegistry::schemaFor(cn)) {
            if (d.name == "Image") { imgDesc = &d; break; }
        }
        std::string cur = imgDesc ? std::get<std::string>(imgDesc->get(inst)) : std::string();
        ImGui::LabelText("Image", "%s", cur.empty() ? "(none)" : cur.c_str());
        if (ImGui::Button(locId(Loc::LocKey::Browse, "##image").c_str()) && imgDesc) {
            std::string path = getPlatform().openFileDialog({{"Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga"}});
            if (!path.empty()) {
                PropValue before = imgDesc->get(inst);
                PropertyRegistry::writeValue(inst, *imgDesc, PropValue(path));
                if (m_history)
                    m_history->record(std::make_unique<SetPropertyCommand>(
                        inst->shared_from_this(), imgDesc, before, PropValue(path)));
            }
        }
    }

    // ---- Workspace ----
    if (inst->IsA("Workspace")) {
        Workspace* ws = static_cast<Workspace*>(inst);
        ImGui::SeparatorText("Workspace");
        ImGui::Checkbox("PhysicsEnabled", &ws->PhysicsEnabled);
        float grav[3] = { ws->Gravity.x, ws->Gravity.y, ws->Gravity.z };
        if (ImGui::DragFloat3("Gravity", grav, 0.1f, -300.0f, 300.0f)) {
            ws->Gravity = Vector3(grav[0], grav[1], grav[2]);
            if (ws->getPhysicsEngine()) ws->getPhysicsEngine()->setGravity(ws->Gravity);
        }
    }

    ImGui::End();
}
