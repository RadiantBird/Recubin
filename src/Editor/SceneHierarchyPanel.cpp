#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/SpawnUtil.hpp>
#include <Editor/CommandHistory.hpp>
#include <algorithm>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Model.hpp>
#include <Instances/Folder.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/CharacterSetting.hpp>
#include <Instances/TextLabel.hpp>
#include <Instances/TextButton.hpp>
#include <Instances/SurfaceGui.hpp>
#include <Instances/BillboardGui.hpp>
#include <Instances/ProximityPrompt.hpp>
#include <Core/AudioService.hpp>
#include <include/imgui/imgui.h>
#include <fstream>
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>

static std::string pickFile() {
    std::string result;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Luau Script (*.luau)", L"*.luau" },
            { L"Luar Script (*.luar)", L"*.luar" },
        };
        pfd->SetFileTypes(2, filters);
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR wpath = nullptr;
                item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) {
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, result.data(), len, nullptr, nullptr);
                }
                CoTaskMemFree(wpath);
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

static std::string pickFolder() {
    std::string result;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD opts; pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS);
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR wpath = nullptr;
                item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) {
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, result.data(), len, nullptr, nullptr);
                }
                CoTaskMemFree(wpath);
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

// ===================================================
//  SceneHierarchyPanel 実装
// ===================================================

SceneHierarchyPanel::SceneHierarchyPanel()
    : EditorPanel("Scene Hierarchy") {}

void SceneHierarchyPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    if (!workspace) {
        ImGui::TextDisabled("(No workspace)");
        ImGui::End();
        return;
    }

    drawNode(systemRoot ? systemRoot : static_cast<Instance*>(workspace));

    // 選択中インスタンスへの右クリックメニュー（ウィンドウ内の空白エリアでも表示）
    if (selectedInstance &&
        ImGui::BeginPopupContextWindow("##hier_wnd_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        renderContextMenu(selectedInstance);
        ImGui::EndPopup();
    }

    renderNewScriptDialog();

    ImGui::End();
}

void SceneHierarchyPanel::drawNode(Instance* inst) {
    if (!inst) return;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    bool inSelection = std::find(selectedInstances.begin(), selectedInstances.end(), inst)
                       != selectedInstances.end();
    if (selectedInstance == inst || inSelection) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool isLeaf = inst->getChildren().empty();
    if (isLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    bool open = ImGui::TreeNodeEx(inst, flags, "%s  [%s]",
                                  inst->Name.c_str(), inst->GetClassName().c_str());
    if (ImGui::IsItemClicked()) {
        if (ImGui::GetIO().KeyCtrl) {
            auto it = std::find(selectedInstances.begin(), selectedInstances.end(), inst);
            if (it != selectedInstances.end()) {
                selectedInstances.erase(it);
                if (selectedInstance == inst)
                    selectedInstance = selectedInstances.empty() ? nullptr : selectedInstances.back();
            } else {
                selectedInstances.push_back(inst);
                selectedInstance = inst;
            }
        } else {
            selectedInstance = inst;
            selectedInstances = { inst };
        }
    }

    // ---- ドラッグソース ----
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("INSTANCE_PTR", &inst, sizeof(Instance*));
        ImGui::Text("%s", inst->Name.c_str());
        ImGui::EndDragDropSource();
    }

    // ---- ドロップターゲット ----
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INSTANCE_PTR")) {
            Instance* dragged = *static_cast<Instance* const*>(payload->Data);
            // 自分自身・子孫へのドロップは禁止
            bool isSelfOrDescendant = false;
            Instance* check = inst;
            while (check) {
                if (check == dragged) { isSelfOrDescendant = true; break; }
                auto p = check->Parent.lock();
                check = p ? p.get() : nullptr;
            }
            if (!isSelfOrDescendant && dragged->Parent.lock() && m_history) {
                auto oldParent = dragged->Parent.lock();
                auto newParent = inst->shared_from_this();
                auto child     = oldParent->children.at(dragged->Name);
                m_history->execute(std::make_unique<MoveInstanceCommand>(oldParent, newParent, child));
                selectedInstance = dragged;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ---- 右クリックコンテキストメニュー ----
    std::string popupId = "ctx##" + std::to_string(reinterpret_cast<uintptr_t>(inst));
    if (ImGui::BeginPopupContextItem(popupId.c_str())) {
        renderContextMenu(inst);
        ImGui::EndPopup();
    }

    if (!isLeaf && open) {
        for (auto& [name, child] : inst->getChildren()) {
            drawNode(child.get());
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::renderNewScriptDialog() {
    if (m_openScriptDialog) {
        ImGui::OpenPopup("New Script");
        m_openScriptDialog = false;
    }

    if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char s_name[128] = "NewScript";
        static int  s_mode = 0; // 0=新規作成, 1=既存ファイル

        ImGui::RadioButton("新規作成",         &s_mode, 0); ImGui::SameLine();
        ImGui::RadioButton("既存ファイルを選択", &s_mode, 1);
        ImGui::Separator();

        if (s_mode == 0) {
            ImGui::Text("Script name:");
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##sname", s_name, sizeof(s_name));
        } else {
            ImGui::TextDisabled("ファイルピッカーで .luau/.luar を選択します");
        }

        if (ImGui::Button("OK", ImVec2(100, 0))) {
            m_pickName     = std::string(s_name);
            m_pickParent   = m_pendingScriptParent;
            m_pickExisting = (s_mode == 1);
            m_doPick       = true;
            m_pendingScriptParent.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pendingScriptParent.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // ポップアップが閉じた後にフォルダ/ファイル選択を実行
    if (m_doPick) {
        m_doPick = false;
        std::string filePath;

        if (m_pickExisting) {
            filePath = pickFile();
        } else {
            std::string folder = pickFolder();
            if (!folder.empty()) {
                filePath = folder + "\\" + m_pickName + ".luau";
                {
                    std::ofstream f(filePath);
                    if (f) f << "-- " << m_pickName << "\n";
                }
            }
        }

        if (!filePath.empty() && m_pickParent && m_history) {
            std::wstring wp(filePath.begin(), filePath.end());
            ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOW);

            // 既存選択時はファイル名をスクリプト名にする
            if (m_pickExisting) {
                auto slash = filePath.find_last_of("/\\");
                std::string fname = (slash == std::string::npos) ? filePath : filePath.substr(slash + 1);
                auto dot = fname.rfind('.');
                bool isLuar = (dot != std::string::npos && fname.substr(dot) == ".luar");
                m_pickName = (dot == std::string::npos || isLuar) ? fname : fname.substr(0, dot);
            }

            auto script = std::make_shared<Script>(filePath);
            script->Name = m_pickName;
            m_history->execute(std::make_unique<AddInstanceCommand>(m_pickParent, script));
        }
        m_pickParent.reset();
    }
}

// ヘルパー: インスタンス名の重複を避けて連番を付ける
static std::string uniqueName(const std::shared_ptr<Instance>& parent, const std::string& base) {
    std::string name = base;
    int n = 1;
    while (parent->children.count(name) > 0)
        name = base + std::to_string(n++);
    return name;
}

void SceneHierarchyPanel::renderInsertMenu(Instance* inst) {
    auto parentSp = inst->shared_from_this();

    // ---- Cube系 ----
    if (ImGui::BeginMenu("Cube系")) {
        auto spawnPos = [this]() {
            return computeSpawnPos(m_user, workspace);
        };
        if (ImGui::MenuItem("Cube") && m_history) {
            auto obj = std::make_shared<Cube>(spawnPos(), Vector3(1, 1, 1), Cube::defaultTextureID);
            obj->Name = uniqueName(parentSp, "Cube");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Cylinder") && m_history) {
            auto obj = std::make_shared<Cylinder>(spawnPos(), Vector3(1, 1, 1));
            obj->Name = uniqueName(parentSp, "Cylinder");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("TriangularPrism") && m_history) {
            auto obj = std::make_shared<TriangularPrism>(spawnPos(), Vector3(1, 1, 1));
            obj->Name = uniqueName(parentSp, "TriangularPrism");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Sphere") && m_history) {
            auto obj = std::make_shared<Sphere>(spawnPos(), Vector3(1, 1, 1));
            obj->Name = uniqueName(parentSp, "Sphere");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        ImGui::EndMenu();
    }

    // ---- 効果 ----
    if (ImGui::BeginMenu("効果")) {
        if (ImGui::MenuItem("Sound", nullptr, false, AudioService::instance != nullptr) && m_history) {
            auto obj = std::make_shared<Sound>(*AudioService::instance);
            obj->Name = uniqueName(parentSp, "Sound");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (AudioService::instance == nullptr)
            ImGui::SetItemTooltip("AudioService が利用できません");
        if (ImGui::MenuItem("Decal") && m_history) {
            auto obj = std::make_shared<Decal>(0, Face::Front);
            obj->Name = uniqueName(parentSp, "Decal");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Texture") && m_history) {
            auto obj = std::make_shared<Texture>(0, Face::Front);
            obj->Name = uniqueName(parentSp, "Texture");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        ImGui::EndMenu();
    }

    // ---- その他 ----
    if (ImGui::BeginMenu("その他")) {
        if (ImGui::MenuItem("Script")) {
            m_pendingScriptParent = parentSp;
            m_openScriptDialog    = true;
        }
        if (ImGui::MenuItem("Folder") && m_history) {
            auto obj = std::make_shared<Folder>();
            obj->Name = uniqueName(parentSp, "Folder");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Model") && m_history) {
            auto obj = std::make_shared<Model>(Vector3(0,0,0), Vector3(1,1,1));
            obj->Name = uniqueName(parentSp, "Model");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Workspace") && m_history) {
            auto obj = std::make_shared<Workspace>();
            obj->Name = uniqueName(parentSp, "Workspace");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Lighting") && m_history) {
            auto obj = std::make_shared<Lighting>();
            obj->Name = uniqueName(parentSp, "Lighting");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("AppImage") && m_history) {
            auto obj = std::make_shared<AppImage>();
            obj->Name = uniqueName(parentSp, "AppImage");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("CharacterSetting") && m_history) {
            auto obj = std::make_shared<CharacterSetting>();
            obj->Name = uniqueName(parentSp, "CharacterSetting");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        ImGui::EndMenu();
    }

    // ---- GUI ----
    if (ImGui::BeginMenu("GUI")) {
        if (ImGui::MenuItem("TextLabel") && m_history) {
            auto obj = std::make_shared<TextLabel>();
            obj->Name = uniqueName(parentSp, "TextLabel");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("TextButton") && m_history) {
            auto obj = std::make_shared<TextButton>();
            obj->Name = uniqueName(parentSp, "TextButton");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("SurfaceGui") && m_history) {
            auto obj = std::make_shared<SurfaceGui>();
            printf("\033[46m pointer: %p\033[0m\n", obj.get());
            obj->Name = uniqueName(parentSp, "SurfaceGui");
            printf("\033[46m %p is a SurfaceGui\033[0m\n", obj.get());
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("BillboardGui") && m_history) {
            auto obj = std::make_shared<BillboardGui>();
                        obj->Name = uniqueName(parentSp, "BillboardGui");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("ProximityPrompt") && m_history) {
            auto obj = std::make_shared<ProximityPrompt>();
            obj->Name = uniqueName(parentSp, "ProximityPrompt");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        ImGui::EndMenu();
    }

    // ---- 物理制約 ----
    if (ImGui::BeginMenu("物理制約")) {
        ImGui::TextDisabled("2つのCube名をPropertiesで設定");
        ImGui::Separator();
        if (ImGui::MenuItem("Weld") && m_history) {
            auto obj = std::make_shared<Weld>();
            obj->Name = uniqueName(parentSp, "Weld");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Motor") && m_history) {
            auto obj = std::make_shared<Motor>();
            obj->Name = uniqueName(parentSp, "Motor");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Rod") && m_history) {
            auto obj = std::make_shared<Rod>();
            obj->Name = uniqueName(parentSp, "Rod");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Rope") && m_history) {
            auto obj = std::make_shared<Rope>();
            obj->Name = uniqueName(parentSp, "Rope");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        ImGui::EndMenu();
    }
}

void SceneHierarchyPanel::renderContextMenu(Instance* inst) {
    if (!inst) return;

    // ---- Workspace 専用ボタン ----
    if (inst->IsA("Workspace")) {
        auto* ws = static_cast<Workspace*>(inst);
        if (ImGui::MenuItem("このworkspaceに切り替える") && onSwitchWorkspace) {
            onSwitchWorkspace(ws);
        }
        if (ImGui::MenuItem("新しいビューポートで開く") && onOpenSecondaryViewport) {
            onOpenSecondaryViewport(ws);
        }
        ImGui::Separator();
    }

    if (ImGui::BeginMenu("Insert Object")) {
        renderInsertMenu(inst);
        ImGui::EndMenu();
    }

    ImGui::Separator();

    // --- Delete ---
    if (ImGui::MenuItem("Delete", "BackSpace") && m_history) {
        auto parent = inst->Parent.lock();
        if (parent) {
            auto childPtr = parent->children.at(inst->Name);
            m_history->execute(std::make_unique<RemoveInstanceCommand>(parent, inst->Name, childPtr));
            selectedInstances.erase(
                std::remove(selectedInstances.begin(), selectedInstances.end(), inst),
                selectedInstances.end());
            if (selectedInstance == inst)
                selectedInstance = selectedInstances.empty() ? nullptr : selectedInstances.back();
        }
    }

    ImGui::Separator();

    // --- Copy ---
    if (ImGui::MenuItem("Copy", "Ctrl+C") && m_clipboard) {
        *m_clipboard = inst->clone();
    }

    // --- Paste (sibling) ---
    bool canPaste = m_clipboard && *m_clipboard;
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste) && m_history) {
        auto parent = inst->Parent.lock();
        if (parent) {
            auto cloned = (*m_clipboard)->clone();
            std::string base = cloned->Name;
            cloned->Name = uniqueName(parent, base);
            m_history->execute(std::make_unique<AddInstanceCommand>(parent, cloned));
        }
    }

    // --- Paste as Child ---
    if (ImGui::MenuItem("Paste as Child", "Ctrl+Shift+V", false, canPaste) && m_history) {
        auto parentSp = inst->shared_from_this();
        auto cloned = (*m_clipboard)->clone();
        std::string base = cloned->Name;
        cloned->Name = uniqueName(parentSp, base);
        m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, cloned));
    }
}
