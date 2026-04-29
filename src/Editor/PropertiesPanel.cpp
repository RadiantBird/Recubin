#include <Editor/PropertiesPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Decal.hpp>
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

void PropertiesPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    Instance* inst = selectedInstance ? *selectedInstance : nullptr;

    if (!inst) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
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
        if (ImGui::Checkbox("Anchored", &bc->Anchored) && m_history) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "Anchored", prevAnchored, bc->Anchored));
        }

        // CanCollide with undo
        bool prevCanCollide = bc->CanCollide;
        if (ImGui::Checkbox("CanCollide", &bc->CanCollide) && m_history) {
            m_history->record(std::make_unique<SetBoolCommand>(
                bcSp, "CanCollide", prevCanCollide, bc->CanCollide));
        }
    }

    // ---- Sound ----
    if (inst->GetClassName() == "Sound") {
        Sound* snd = static_cast<Sound*>(inst);
        ImGui::SeparatorText("Sound");
        ImGui::LabelText("ContentPath", "%s", snd->getContentPath().c_str());
        if (ImGui::Button("参照...##sound")) {
            std::string path = browseFile(L"Audio (*.mp3;*.wav;*.ogg)", L"*.mp3;*.wav;*.ogg");
            if (!path.empty()) {
                YAML::Node node; node = path;
                snd->setProperty("ContentPath", node);
            }
        }
        ImGui::Checkbox("AutoPlay", &snd->autoPlay);
        bool looping = snd->isLooping();
        if (ImGui::Checkbox("Looped", &looping)) snd->setLooping(looping);
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
        ImGui::SeparatorText("Decal");
        ImGui::LabelText("Texture", "%s", dcl->texturePath.c_str());
        if (ImGui::Button("参照...##decal")) {
            std::string path = browseFile(L"Image (*.png;*.jpg;*.bmp;*.tga)", L"*.png;*.jpg;*.bmp;*.tga");
            if (!path.empty()) {
                YAML::Node node; node = path;
                dcl->setProperty("Texture", node);
            }
        }
    }

    ImGui::End();
}
