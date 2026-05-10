#include <Editor/PropertiesPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Util/Color4.hpp>
#include <include/imgui/imgui.h>
#include <unordered_map>
#include <string>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

// ===================================================
//  PropertiesPanel 実装
// ===================================================

static std::string browseFile(const wchar_t* filterName, const wchar_t* filterSpec) {
    std::string result;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        COMDLG_FILTERSPEC filter = { filterName, filterSpec };
        pfd->SetFileTypes(1, &filter);
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
                          std::shared_ptr<BaseCube> bc,
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

        float w = ImGui::GetContentRegionAvail().x - 34.0f;
        if (w < 60.0f) w = 60.0f;
        ImGui::SetNextItemWidth(w);

        std::string txtId = std::string("##txt_") + id;
        if (ImGui::InputText(txtId.c_str(), buf, sizeof(buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            float x = val.x, y = val.y, z = val.z;
            if (sscanf(buf, "%f , %f , %f", &x, &y, &z) == 3 ||
                sscanf(buf, "%f,%f,%f",     &x, &y, &z) == 3) {
                Vector3 newVal(x, y, z);
                if (history && bc) {
                    history->execute(std::make_unique<SetVec3Command>(bc, prop, val, newVal));
                } else if (bc && prop == "Position") {
                    bc->teleportTo(newVal);
                } else if (bc && prop == "Size") {
                    bc->setSize(newVal);
                } else {
                    val = newVal;
                }
            }
        }
        ImGui::SameLine();
        std::string btnId = std::string(expanded ? "-##col_" : "+##exp_") + id;
        if (ImGui::SmallButton(btnId.c_str())) expanded = !expanded;
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
            if (bc && prop == "Position") bc->teleportTo(newVal);
            else if (bc && prop == "Size") bc->setSize(newVal);
            else val = newVal;
        }

        if (ImGui::IsItemDeactivatedAfterEdit() && history && bc) {
            Vector3 after(arr[0], arr[1], arr[2]);
            history->record(std::make_unique<SetVec3Command>(bc, prop, s_before[key], after));
        }

        ImGui::PopID();
        ImGui::Unindent(12.0f);
    }
}

// キューブのワークスペース相対パスを返す（例: "FolderA\CubeName" or "CubeName"）
static std::string cubeRelativePath(Instance* cube) {
    Instance* ws = cube->findFirstAncestorWorkspace();
    if (!ws) return cube->Name;
    std::vector<std::string> parts;
    Instance* cur = cube;
    while (cur) {
        auto par = cur->Parent.lock();
        parts.push_back(cur->Name);
        if (!par || par.get() == ws) break;
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
    if (inst && inst->Parent.expired()) {
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
        ImGui::TextUnformatted("Viewport でキューブをクリックして指定");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ---- 基本情報 ----
    ImGui::SeparatorText("Instance");
    ImGui::LabelText("ClassName", "%s", inst->GetClassName().c_str());
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
        std::shared_ptr<BaseCube> bcSp;
        if (inst->IsA("BaseCube")) {
            bcSp = std::static_pointer_cast<BaseCube>(inst->shared_from_this());
        }

        ImGui::SeparatorText("Transform");

        ImGui::Text("Position");
        ImGui::SameLine(80.0f);
        drawVec3Field("Position", s->Position, 0.05f, -1e9f, 1e9f, bcSp, "Position", m_history);

        ImGui::Text("Size");
        ImGui::SameLine(80.0f);
        drawVec3Field("Size", s->Size, 0.05f, 0.01f, 1000.0f, bcSp, "Size", m_history);

        // Rotation (Euler 角, 度数)
        ImGui::Text("Rotation");
        ImGui::SameLine(80.0f);
        {
            static std::unordered_map<std::string, Vector3> s_rotBefore;
            Vector3 euler = s->cframe.Rotation.toEuler();
            float rot[3] = { euler.x, euler.y, euler.z };
            float rotW = ImGui::GetContentRegionAvail().x;
            if (rotW < 60.0f) rotW = 60.0f;
            ImGui::SetNextItemWidth(rotW);
            ImGui::PushID("Rotation");
            bool rotChanged = ImGui::DragFloat3("##rot", rot, 1.0f, -360.0f, 360.0f, "%.1f");
            if (ImGui::IsItemActivated()) s_rotBefore["rot"] = euler;
            if (rotChanged) s->cframe.Rotation = Quaternion::fromEuler(Vector3(rot[0], rot[1], rot[2]));
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                Quaternion qBefore = Quaternion::fromEuler(s_rotBefore["rot"]);
                Quaternion qAfter  = Quaternion::fromEuler(Vector3(rot[0], rot[1], rot[2]));
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
    }

    // ---- Sound ----
    if (inst->GetClassName() == "Sound") {
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

        if (ImGui::Button("Play"))  snd->play();
        ImGui::SameLine();
        if (ImGui::Button("Stop"))  snd->stop();
    }

    // ---- Script ----
    if (inst->GetClassName() == "Script") {
        Script* sc = static_cast<Script*>(inst);
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
    }

    // ---- Decal ----
    if (inst->GetClassName() == "Decal") {
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

    // ---- Lighting ----
    if (inst->GetClassName() == "Lighting") {
        Lighting* lt = static_cast<Lighting*>(inst);
        auto ltSp = std::static_pointer_cast<Lighting>(inst->shared_from_this());
        ImGui::SeparatorText("Lighting");

        // Light Direction with undo
        {
            static Vector3 s_dirBefore;
            float dir[3] = { lt->lightDir.x, lt->lightDir.y, lt->lightDir.z };
            bool changed = ImGui::DragFloat3("Direction", dir, 0.01f, -1.0f, 1.0f, "%.3f");
            if (ImGui::IsItemActivated()) s_dirBefore = lt->lightDir;
            if (changed) lt->lightDir = Vector3(dir[0], dir[1], dir[2]);
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                Vector3 after(dir[0], dir[1], dir[2]);
                m_history->record(std::make_unique<SetLightDirCommand>(ltSp, s_dirBefore, after));
            }
        }

        // Brightness with undo
        {
            static float s_brightBefore;
            bool changed = ImGui::DragFloat("Brightness", &lt->brightness, 0.01f, 0.0f, 5.0f, "%.2f");
            if (ImGui::IsItemActivated()) s_brightBefore = lt->brightness;
            if (ImGui::IsItemDeactivatedAfterEdit() && m_history) {
                m_history->record(std::make_unique<SetLightBrightnessCommand>(ltSp, s_brightBefore, lt->brightness));
            }
        }

    }

    // ---- Skybox ----
    if (inst->GetClassName() == "Skybox") {
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
    if (inst->GetClassName() == "Rope") {
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
    }

    // ---- Rod ----
    if (inst->GetClassName() == "Rod") {
        Rod* rod = static_cast<Rod*>(inst);
        auto rodSp = std::static_pointer_cast<Rod>(inst->shared_from_this());
        ImGui::SeparatorText("Rod");
        drawConstraintCubeRef("Cube0", rod->m_cube0Name, "Cube0", rodSp);
        drawConstraintCubeRef("Cube1", rod->m_cube1Name, "Cube1", rodSp);
    }

    // ---- Weld ----
    if (inst->GetClassName() == "Weld") {
        Weld* weld = static_cast<Weld*>(inst);
        auto weldSp = std::static_pointer_cast<Weld>(inst->shared_from_this());
        ImGui::SeparatorText("Weld");
        drawConstraintCubeRef("Cube0", weld->m_cube0Name, "Cube0", weldSp);
        drawConstraintCubeRef("Cube1", weld->m_cube1Name, "Cube1", weldSp);
    }

    // ---- Motor ----
    if (inst->GetClassName() == "Motor") {
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

    ImGui::End();
}
