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

void SceneHierarchyPanel::renderInsertMenu(Instance* inst) {
    auto parentSp = inst->shared_from_this();

    // ---- Cube系 ----
    if (ImGui::BeginMenu("Cube系")) {
        auto spawnPos = computeSpawnPos(m_user, workspace);
        
        tryInsertInstance<Cube>(m_history, "Cube", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Cylinder>(m_history, "Cylinder", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<TriangularPrism>(m_history, "TriangularPrism", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<Sphere>(m_history, "Sphere", parentSp, spawnPos, Vector3(1, 1, 1));
        
        ImGui::EndMenu();
    }

    // ---- 効果 ----
    if (ImGui::BeginMenu("効果")) {
        if (ImGui::MenuItem("Sound", nullptr, false, AudioService::instance != nullptr) && m_history) {
            auto obj = std::make_shared<Sound>(*AudioService::instance);
            obj->Name = uniqueName(parentSp, "Sound");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (AudioService::instance == nullptr) {
            ImGui::SetItemTooltip("AudioService が利用できません");
        }
        tryInsertInstance<Decal>(m_history, "Decal", parentSp, 0, Face::Front);
        tryInsertInstance<Texture>(m_history, "Texture", parentSp, 0, Face::Front);
        
        ImGui::EndMenu();
    }

    // ---- その他 ----
    if (ImGui::BeginMenu("その他")) {
        // Scriptはダイアログを開く特殊な挙動なのでそのまま
        if (ImGui::MenuItem("Script")) {
            m_pendingScriptParent = parentSp;
            m_openScriptDialog    = true;
        }
        
        tryInsertInstance<Folder>(m_history, "Folder", parentSp);
        tryInsertInstance<Model>(m_history, "Model", parentSp, Vector3(0, 0, 0), Vector3(1, 1, 1));
        tryInsertInstance<Workspace>(m_history, "Workspace", parentSp);
        tryInsertInstance<Lighting>(m_history, "Lighting", parentSp);
        tryInsertInstance<AppImage>(m_history, "AppImage", parentSp);
        tryInsertInstance<CharacterSetting>(m_history, "CharacterSetting", parentSp);
        
        ImGui::EndMenu();
    }

    // ---- GUI ----
    if (ImGui::BeginMenu("GUI")) {
        tryInsertInstance<TextLabel>(m_history, "TextLabel", parentSp);
        tryInsertInstance<TextButton>(m_history, "TextButton", parentSp);
        tryInsertInstance<SurfaceGui>(m_history, "SurfaceGui", parentSp);
        tryInsertInstance<BillboardGui>(m_history, "BillboardGui", parentSp);
        tryInsertInstance<ProximityPrompt>(m_history, "ProximityPrompt", parentSp);
        
        ImGui::EndMenu();
    }

    // ---- 物理制約 ----
    if (ImGui::BeginMenu("物理制約")) {
        ImGui::TextDisabled("2つのCube名をPropertiesで設定");
        ImGui::Separator();
        
        tryInsertInstance<Weld>(m_history, "Weld", parentSp);
        tryInsertInstance<Motor>(m_history, "Motor", parentSp);
        tryInsertInstance<Rod>(m_history, "Rod", parentSp);
        tryInsertInstance<Rope>(m_history, "Rope", parentSp);
        
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
        if (ImGui::MenuItem("新しいビューポートで開く(非推奨、バグあり)") && onOpenSecondaryViewport) {
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
