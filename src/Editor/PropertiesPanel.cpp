#include <Editor/PropertiesPanel.hpp>
#include <Editor/CommandHistory.hpp>
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
#include <Util/Color4.hpp>
#include <Util/FileDialog.hpp>
#include <include/imgui/imgui.h>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <windows26.h>
#include <shellapi.h>
#include <shobjidl.h>

// ===================================================
//  PropertiesPanel 実装
// ===================================================

// フォルダ選択ダイアログ（FOS_PICKFOLDERS）。Terrain の DataPath 等に使用。
static std::string browseFolder() {
    std::string result;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD opts = 0; pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR wpath = nullptr;
                item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) { result.resize(len - 1); WideCharToMultiByte(CP_UTF8, 0, wpath, -1, result.data(), len, nullptr, nullptr); }
                CoTaskMemFree(wpath);
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

// ===================================================
//  スキーマ駆動インスペクタ
//  PropertyRegistry に登録済みのクラスは、ここが表を辿って自動でウィジェットを
//  描画し、編集を汎用 SetPropertyCommand として記録する（Undo / dirty 対応）。
//  → スキーマに1行足すだけでインスペクタに反映され、エディター取り残しを防ぐ。
// ===================================================
static void renderSchemaInspector(Instance* inst, const char* className, CommandHistory* history) {
    static PropValue s_before;  // 編集開始時の値（同時編集は1つなので単一でよい）
    // own-only（既存の per-level エディターブロック構造を保つ。基底は各 IsA ブロックで描画）
    for (const auto& d : PropertyRegistry::schemaFor(className)) {
        const PropertyDesc* dp = &d;
        if (d.kind != PropKind::Field || !d.editable || !d.get) continue;
        const bool readOnly = !d.set;
        std::string label(d.name);
        ImGui::PushID(static_cast<int>(reinterpret_cast<std::uintptr_t>(dp)));
        if (readOnly) ImGui::BeginDisabled();

        PropValue cur = d.get(inst);
        switch (d.type) {
            case PropType::Float: {
                float v = std::get<float>(cur);
                if (ImGui::DragFloat(label.c_str(), &v, d.step, d.lo, d.hi, "%.2f")) d.set(inst, PropValue(v));
                break;
            }
            case PropType::Int: {
                int v = std::get<int>(cur);
                if (ImGui::DragInt(label.c_str(), &v, 1.0f, (int)d.lo, (int)d.hi)) d.set(inst, PropValue(v));
                break;
            }
            case PropType::Bool: {
                bool v = std::get<bool>(cur);
                if (ImGui::Checkbox(label.c_str(), &v)) d.set(inst, PropValue(v));
                break;
            }
            case PropType::String: {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s", std::get<std::string>(cur).c_str());
                if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) d.set(inst, PropValue(std::string(buf)));
                break;
            }
            case PropType::Vec3: {
                Vector3 v = std::get<Vector3>(cur);
                if (ImGui::DragFloat3(label.c_str(), &v.x, d.step, d.lo, d.hi, "%.2f")) d.set(inst, PropValue(v));
                break;
            }
            case PropType::Vec2: {
                Vector2 v = std::get<Vector2>(cur);
                if (ImGui::DragFloat2(label.c_str(), &v.x, d.step, d.lo, d.hi, "%.2f")) d.set(inst, PropValue(v));
                break;
            }
            case PropType::Color4: {
                Color4 v = std::get<Color4>(cur);
                if (ImGui::ColorEdit4(label.c_str(), &v.r)) d.set(inst, PropValue(v));
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
                    d.set(inst, PropValue(d.enumNames[idx].second));
                break;
            }
        }

        // 編集の開始/確定を捉えて Undo 1ステップとして記録する
        if (ImGui::IsItemActivated())
            s_before = PropertyRegistry::readValue(inst, d);
        if (ImGui::IsItemDeactivatedAfterEdit() && history) {
            PropValue after = PropertyRegistry::readValue(inst, d);
            history->record(std::make_unique<SetPropertyCommand>(
                inst->shared_from_this(), dp, s_before, after));
        }
        if (readOnly) ImGui::EndDisabled();
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

        float w = ImGui::GetContentRegionAvail().x - 56.0f;  // [+/-] と [丸] の2ボタン分を確保
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
        std::string roundId = std::string("丸##round_") + id;
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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("整数に丸める");
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

// キューブの相対パスを返す。Workspace 配下なら Workspace 相対（例: "FolderA\CubeName"）、
// Workspace 外（StarterCharacter 等）なら最上位祖先(System)相対（例: "StarterCharacter\Head"）。
// resolveConstraintRefs / Weld::setProperty 側の解決規約と一致させる。
static std::string cubeRelativePath(Instance* cube) {
    Instance* stopAt = cube->findFirstAncestorWorkspace();
    if (!stopAt) {
        // Workspace 外: 最上位の祖先（System 等）を起点にする
        Instance* top = cube;
        for (auto p = cube->Parent.lock(); p; p = p->Parent.lock()) top = p.get();
        stopAt = top;
    }
    std::vector<std::string> parts;
    Instance* cur = cube;
    while (cur) {
        auto par = cur->Parent.lock();
        parts.push_back(cur->Name);
        if (!par || par.get() == stopAt) break;
        cur = par.get();
    }
    std::reverse(parts.begin(), parts.end());
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); i++) result += "\\" + parts[i];
    return result;
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
    strncpy_s(buf, nameRef.c_str(), sizeof(buf) - 1);
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
            m_picker->active     = true;
            m_picker->prop       = prop;
            m_picker->constraint = inst.get();
            m_picker->onPick = [inst, propStr = std::string(prop),
                                 nameRefPtr = &nameRef, hist = m_history]
                               (std::shared_ptr<BaseCube> cube) {
                std::string before = *nameRefPtr;
                std::string after  = cubeRelativePath(cube.get());
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
    // ツリーから除去済み（Parent expired）なインスタンスは選択解除
    // System はツリーのルートで元々親を持たない（Parent が常に expired）ため対象外にする
    if (inst && inst->Parent.expired() && !inst->IsA("System")) {
        *selectedInstance = nullptr;
        inst = nullptr;
    }

    if (!inst) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
    }

    if (m_picker && m_picker->active) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
        ImGui::TextUnformatted("Viewport またはヒエラルキーでキューブをクリックして指定");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ---- 基本情報 ----
    ImGui::SeparatorText("Instance");
    ImGui::LabelText("ClassName", "%s", inst->getClassName().c_str());
    ImGui::LabelText("Path",      "%s", inst->getFullPath().c_str());

    static std::string s_nameBefore;
    char nameBuf[256] = {};
    strncpy_s(nameBuf, inst->Name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        inst->Name = std::string(nameBuf);
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

    // ---- BaseCube (Color / Anchored / CanCollide) ----
    if (inst->IsA("BaseCube")) {
        BaseCube* bc = static_cast<BaseCube*>(inst);
        auto bcSp = std::static_pointer_cast<BaseCube>(inst->shared_from_this());

        ImGui::SeparatorText("Appearance");

        // Color with undo
        static Color4 s_colorBefore;
        float col[4] = { bc->Color.r, bc->Color.g, bc->Color.b, bc->Color.a };
        if (ImGui::IsItemActivated()) s_colorBefore = bc->Color;
        bool colorChanged = ImGui::ColorEdit4("Color", col);
        if (ImGui::IsItemActivated()) s_colorBefore = bc->Color;
        if (colorChanged) bc->Color = Color4(col[0], col[1], col[2], col[3]);
        if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
            Color4 after(col[0], col[1], col[2], col[3]);
            m_history->record(std::make_unique<SetColorCommand>(bcSp, s_colorBefore, after));
        }

        ImGui::SeparatorText("Physics");

        // Anchored with undo
        bool prevAnchored = bc->Anchored;
        bool anchored = bc->Anchored;
        if (ImGui::Checkbox("Anchored", &anchored)) {
            bc->setAnchored(anchored);
            if (m_history)
                m_history->record(std::make_unique<SetBoolCommand>(
                    bcSp, "Anchored", prevAnchored, anchored));
        }

        // CanCollide with undo
        bool prevCanCollide = bc->CanCollide;
        if (ImGui::Checkbox("CanCollide", &bc->CanCollide) && m_history && prevCanCollide != bc->CanCollide) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "CanCollide", prevCanCollide, bc->CanCollide));
        }

        // CastShadow with undo
        bool prevCastShadow = bc->CastShadow;
        if (ImGui::Checkbox("CastShadow", &bc->CastShadow) && m_history && prevCastShadow != bc->CastShadow) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "CastShadow", prevCastShadow, bc->CastShadow));
        }

        // Unlit with undo
        bool prevUnlit = bc->Unlit;
        if (ImGui::Checkbox("Unlit", &bc->Unlit) && m_history && prevUnlit != bc->Unlit) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "Unlit", prevUnlit, bc->Unlit));
        }

        // MassDensity with undo（ドラッグ中は値だけ更新し、確定時に actor 再生成 + undo 記録）
        static float s_massDensityBefore;
        ImGui::DragFloat("MassDensity", &bc->MassDensity, 0.01f, 0.01f, 50.0f, "%.2f");
        if (ImGui::IsItemActivated()) s_massDensityBefore = bc->MassDensity;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            bc->setMassDensity(bc->MassDensity);  // actor を再生成して物理に反映
            if (m_history)
                m_history->record(std::make_unique<SetMassDensityCommand>(bcSp, s_massDensityBefore, bc->MassDensity));
        }

        // ---- Material（プリセット + 数値微調整） ----
        ImGui::SeparatorText("Material");

        // MaterialType プリセット: 選択で3値を既定値に上書き
        static const char* matItems[] = { "Plastic", "Wood", "Metal", "Stone" };
        int matIdx = static_cast<int>(bc->material.type);
        if (ImGui::Combo("MaterialType", &matIdx, matItems, 4)) {
            Material before = bc->material;
            Material after  = Material::GetDefault(static_cast<MaterialType>(matIdx));
            bc->setMaterial(after);
            if (m_history)
                m_history->record(std::make_unique<SetMaterialCommand>(bcSp, before, after));
        }

        // friction/restitution の個別微調整。ドラッグ中は値だけ更新し、
        // 確定時に actor 再生成 + undo 記録（毎フレームの actor 再生成を回避）。
        static Material s_matBefore;
        auto matDrag = [&](const char* label, float* field) {
            ImGui::DragFloat(label, field, 0.01f, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemActivated()) s_matBefore = bc->material;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                bc->setMaterial(bc->material);  // actor を再生成して物理に反映
                if (m_history)
                    m_history->record(std::make_unique<SetMaterialCommand>(bcSp, s_matBefore, bc->material));
            }
        };
        matDrag("StaticFriction",  &bc->material.staticFriction);
        matDrag("DynamicFriction", &bc->material.dynamicFriction);
        matDrag("Restitution",     &bc->material.restitution);
    }

    // ---- MeshCube ----
    if (inst->getClassName() == "MeshCube") {
        MeshCube* mc = static_cast<MeshCube*>(inst);
        ImGui::SeparatorText("MeshCube");
        ImGui::LabelText("MeshFile", "%s", mc->MeshFile.empty() ? "(none)" : mc->MeshFile.c_str());
        if (ImGui::Button("参照...##meshcube")) {
            std::string path = browseFile(L"GLB (*.glb)", L"*.glb");
            if (!path.empty()) {
                YAML::Node node; node = path;
                mc->setProperty("MeshFile", node);
            }
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
        if (ImGui::Button("参照...##fileref")) {
            std::string path = browseFile(L"All files (*.*)", L"*.*");
            if (!path.empty()) fr->Path = path;
        }
    }

    if (inst->getClassName() == "Sound") {
        Sound* snd = static_cast<Sound*>(inst);
        auto sndSp = std::static_pointer_cast<Sound>(inst->shared_from_this());
        ImGui::SeparatorText("Sound");
        ImGui::LabelText("ContentPath", "%s", snd->getContentPath().c_str());
        if (ImGui::Button("参照...##sound")) {
            std::string path = browseFile(L"Audio (*.mp3;*.wav;*.ogg)", L"*.mp3;*.wav;*.ogg");
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

        if (ImGui::Button("Play"))  snd->play();
        ImGui::SameLine();
        if (ImGui::Button("Stop"))  snd->stop();
        ImGui::SameLine();
        if (ImGui::Button("Reset")) snd->reset();
    }

    // ---- Script ----
    if (inst->getClassName() == "Script") {
        Script* sc = static_cast<Script*>(inst);
        auto scSp = std::static_pointer_cast<Script>(inst->shared_from_this());
        ImGui::SeparatorText("Script");
        ImGui::LabelText("Source", "%s", sc->Path.c_str());
        if (ImGui::Button("参照...##script")) {
            std::string path = browseFile(L"Luau Script (*.luau;*.lua)", L"*.luau;*.lua");
            if (!path.empty()) {
                YAML::Node node; node = path;
                sc->setProperty("Path", node);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("外部エディタで開く") && !sc->Path.empty()) {
            std::wstring wp(sc->Path.begin(), sc->Path.end());
            ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOW);
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

        // Face combo with undo
        {
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
        if (ImGui::Button("参照...##decal")) {
            std::string path = browseFile(L"Image (*.png;*.jpg;*.bmp;*.tga)", L"*.png;*.jpg;*.bmp;*.tga");
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
        if (ImGui::Button("参照...##tex")) {
            std::string path = browseFile(L"Image (*.png;*.jpg;*.bmp;*.tga)", L"*.png;*.jpg;*.bmp;*.tga");
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
        ImGui::SeparatorText("System (Safety Limits)");

        renderSchemaInspector(inst, "System", m_history);

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
            ImGui::TextDisabled("→ %s", tool->Handle->Name.c_str());
        } else if (!tool->m_handleName.empty()) {
            ImGui::TextDisabled("→ (未解決)");
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
            std::string btnLabel = std::string(propName) + " 参照...##" + idSuffix;
            if (ImGui::Button(btnLabel.c_str())) {
                std::string path = browseFile(L"Audio (*.mp3;*.wav;*.ogg)", L"*.mp3;*.wav;*.ogg");
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
        if (ImGui::Button("参照...##terraindp")) {
            std::string folder = browseFolder();
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
        if (ImGui::Button("乱数化##terrainseed")) {
            int before = terrain->Seed;
            std::random_device rd;
            terrain->Seed = static_cast<int>(rd());
            if (m_history) m_history->record(std::make_unique<SetTerrainIntCommand>(terrSp, "Seed", before, terrain->Seed));
        }
        {
            bool before = terrain->Flat;
            bool value  = terrain->Flat;
            if (ImGui::Checkbox("Flat（平坦生成）##terrain", &value)) {
                terrain->Flat = value;
                if (m_history) m_history->record(std::make_unique<SetTerrainBoolCommand>(terrSp, "Flat", before, value));
            }
        }

        if (ImGui::Button("再生成##terrainregen")) {
            ImGui::OpenPopup("Terrain再生成の確認");
        }
        if (ImGui::BeginPopupModal("Terrain再生成の確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("DataPath の地形（ブラシ編集を含む）を破棄して、");
            ImGui::Text("現在の Seed / Flat 設定で作り直します。よろしいですか？");
            ImGui::Separator();
            if (ImGui::Button("再生成する", ImVec2(120, 0))) {
                if (terrain->streamer) {
                    terrain->streamer->regenerate(static_cast<uint32_t>(terrain->Seed), terrain->Flat);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_terrainBrush) {
            ImGui::Separator();
            ImGui::Checkbox("ブラシで編集", &m_terrainBrush->active);
            if (m_terrainBrush->active) {
                ImGui::SliderFloat("半径", &m_terrainBrush->radius, 1.0f, 64.0f, "%.1f studs");
                static const char* modeItems[] = { "Lower（削る）", "Smooth（滑らかに）", "Raise（盛る）" };
                int modeIdx = m_terrainBrush->mode + 1; // -1,0,+1 -> 0,1,2
                if (ImGui::Combo("モード", &modeIdx, modeItems, 3)) {
                    m_terrainBrush->mode = modeIdx - 1;
                }
                ImGui::TextDisabled("ビューポート上で左クリック長押しで適用");
            }
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
            std::string btnId = std::string("参照...##skybox") + std::to_string(i);
            if (ImGui::Button(btnId.c_str())) {
                std::string path = browseFile(L"Image (*.png;*.jpg;*.bmp;*.tga)", L"*.png;*.jpg;*.bmp;*.tga");
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
        if (ImGui::Button("参照...##appimage")) {
            std::string path = browseFile(L"Image (*.png;*.jpg;*.bmp;*.ico)", L"*.png;*.jpg;*.bmp;*.ico");
            if (!path.empty()) {
                YAML::Node node; node = path;
                ai->setProperty("IconPath", node);
            }
        }
    }

    // ---- Humanoid（スキーマ駆動。プロパティ追加はスキーマに1行足すだけ） ----
    if (inst->getClassName() == "Humanoid") {
        ImGui::SeparatorText("Humanoid");
        renderSchemaInspector(inst, "Humanoid", m_history);
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
    if (inst->getClassName() == "BillboardGui") {
        ImGui::SeparatorText("BillboardGui");
        renderSchemaInspector(inst, "BillboardGui", m_history);
    }
    if (inst->getClassName() == "ProximityPrompt") {
        ImGui::SeparatorText("ProximityPrompt");
        renderSchemaInspector(inst, "ProximityPrompt", m_history);
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
        if (ImGui::Button("参照...##image") && imgDesc) {
            std::string path = browseFile(L"Image (*.png;*.jpg;*.bmp;*.tga)", L"*.png;*.jpg;*.bmp;*.tga");
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
