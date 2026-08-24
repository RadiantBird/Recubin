#include <Editor/SceneHierarchyPanel.hpp>
#include "include/Editor/IconsDef.hpp"
#include <Editor/SpawnUtil.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/SceneHierarchyGrouping.hpp>
#include <Editor/PropertiesPanel.hpp>  // PickerState の定義
#include <Editor/Localization.hpp>
#include <Editor/GuiAutomation.hpp>
#include <Instances/BaseCube.hpp>
#include <algorithm>
#include <unordered_set>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Truss.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Script.hpp>
#include <Instances/LocalScript.hpp>
#include <Instances/ModuleScript.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Decal.hpp>
#include <Instances/SurfaceMark.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Canvas.hpp>
#include <Instances/Highlight.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/PointLight.hpp>
#include <Instances/SpotLight.hpp>
#include <Instances/PostEffect.hpp>
#include <Core/Terrain.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Rod.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Force.hpp>
#include <Instances/Model.hpp>
#include <Instances/Folder.hpp>
#include <Instances/FileRef.hpp>
#include <Instances/FontFile.hpp>
#include <Instances/TextFile.hpp>
#include <Instances/Tool.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
#include <Instances/SignalEvent.hpp>
#include <Instances/StarterCharacter.hpp>
#include <Instances/TextLabel.hpp>
#include <Instances/TextButton.hpp>
#include <Instances/SurfaceGui.hpp>
#include <Instances/BillboardGui.hpp>
#include <Instances/ProximityPrompt.hpp>
#include <Instances/ImageLabel.hpp>
#include <Instances/ImageButton.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Weather.hpp>
#include <Instances/IntValue.hpp>
#include <Instances/BoolValue.hpp>
#include <Instances/NumberValue.hpp>
#include <Instances/Vector3Value.hpp>
#include <Instances/Color4Value.hpp>
#include <Instances/CFrameValue.hpp>
#include <Instances/QuaternionValue.hpp>
#include <Instances/ObjectValue.hpp>
#include <Core/AudioService.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Logger.hpp>
#include <Util/AssetPath.hpp>
#include <include/imgui/imgui.h>
#include <fstream>
#include <filesystem>

static bool hierarchyContainsInstance(Instance* root, Instance* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    for (auto& [name, child] : root->children) {
        if (hierarchyContainsInstance(child.get(), target)) return true;
    }
    return false;
}

static std::string pickFile() {
    return getPlatform().openFileDialog({
        {"Luau Script (*.luau)", "*.luau"},
        {"Luar Script (*.luar)", "*.luar"},
    });
}

static std::string pickFolder() {
    return getPlatform().openFolderDialog();
}

static std::string textFileProjectRelative(const std::string& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    const auto project = std::filesystem::current_path(ec);
    if (ec) return AssetPath::toStored(std::filesystem::path(path));
    const auto relative = std::filesystem::relative(absolute, project, ec);
    if (ec || relative.empty()) return AssetPath::toStored(absolute);
    const auto stored = AssetPath::toStored(relative);
    if (stored == ".." || stored.rfind("../", 0) == 0) return AssetPath::toStored(absolute);
    return stored;
}

static bool isValidNewScriptName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return std::any_of(name.begin(), name.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
}

static constexpr const char* SCRIPT_NAME_ERROR =
    "Enter a name that is not blank, '.', '..', or a path.";
static constexpr const char* SCRIPT_EXISTS_ERROR =
    "A script with that name already exists in the selected folder.";
static constexpr const char* SCRIPT_PATH_ERROR =
    "The selected folder path could not be resolved.";
static constexpr const char* SCRIPT_WRITE_ERROR =
    "The script file could not be written.";

// ===================================================
//  SceneHierarchyPanel 実装
// ===================================================

SceneHierarchyPanel::SceneHierarchyPanel()
    : EditorPanel("Explorer") {} // "Scene Hierarchy" を "Explorer" に改名

void SceneHierarchyPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    if (!workspace) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::NoWorkspace));
        ImGui::End();
        return;
    }

    Instance* root = systemRoot ? systemRoot : static_cast<Instance*>(workspace);
    selectedInstances.erase(std::remove_if(selectedInstances.begin(), selectedInstances.end(),
        [root](Instance* inst) { return !hierarchyContainsInstance(root, inst); }),
        selectedInstances.end());
    if (!hierarchyContainsInstance(root, selectedInstance)) {
        selectedInstance = selectedInstances.empty() ? nullptr : selectedInstances.back();
    }

    drawNode(root);

    // ドラッグ＆ドロップで積まれた親変更を、走査完了後にまとめて実行する
    // （走査中の children マップ変更による iterator 無効化＝表示崩れを回避）
    if (!readOnly && !m_pendingReparents.empty() && m_history) {
        auto group = std::make_unique<CompositeCommand>();
        for (auto& pr : m_pendingReparents) {
            if (pr.oldParent && pr.newParent && pr.child &&
                pr.oldParent->children.find(pr.child->Name) != pr.oldParent->children.end())
                group->add(std::make_unique<MoveInstanceCommand>(pr.oldParent, pr.newParent, pr.child));
        }
        if (!group->empty()) {
            m_history->execute(std::move(group));
            if (m_pendingSelect) selectedInstance = m_pendingSelect;
        }
    }
    m_pendingReparents.clear();
    m_pendingSelect = nullptr;

    // 選択中インスタンスへの右クリックメニュー（ウィンドウ内の空白エリアでも表示）
    if (!readOnly && selectedInstance &&
        ImGui::BeginPopupContextWindow("##hier_wnd_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        renderContextMenu(selectedInstance);
        ImGui::EndPopup();
    }

    if (!readOnly) {
        renderNewScriptDialog();
        renderNewTerrainDialog();
        renderTextFileDialog();
    }

    ImGui::End();
}

static const char* getClassIcon(const std::string& cn) {
    if (cn == "Workspace")                                                   return ICON_WORKSPACE;
    if (cn == "Terrain")                                                     return ICON_TERRAIN;
    if (cn == "Lighting" || cn == "PointLight" || cn == "SpotLight")         return ICON_LIGHT;
    if (cn == "Skybox")                                                      return ICON_SKYBOX;
    if (cn == "Model")                                                       return ICON_MODEL;
    if (cn == "Folder")                                                      return ICON_FOLDER;
    if (cn == "Tool")                                                        return ICON_TOOL;
    if (cn == "Script" || cn == "LocalScript" || cn == "ModuleScript")       return ICON_SCRIPT;
    if (cn == "Sound")                                                       return ICON_SOUND;
    if (cn == "Humanoid")                                                    return ICON_HUMANOID;
    if (cn == "User")                                                        return ICON_USER;
    if (cn == "Decal" || cn == "Texture" || cn == "SurfaceMark" || cn == "Canvas" ||
        cn == "ImageLabel" || cn == "ImageButton")                           return ICON_DECAL;
    if (cn == "FileRef")                                                     return ICON_FILE;
    if (cn == "Sphere")                                                      return ICON_SPHERE;
    if (cn == "Cube"   || cn == "Cylinder" || cn == "LiquidCube" || cn == "SpawnLocation" ||
        cn == "TriangularPrism" || cn == "Truss" || cn == "Seat")            return ICON_CUBE;
    if (cn == "MeshCube") return ICON_MESHCUBE;
    if (cn == "TextLabel"  || cn == "TextButton" || cn == "GuiButton" ||
        cn == "ScreenGui"  || cn == "SurfaceGui" || cn == "BillboardGui" ||
        cn == "WorldGuiObject")                                              return ICON_GUI;
    if (cn == "Rope" || cn == "Rod" || cn == "BallSocket" || cn == "NoCollision" || cn == "Weld" || cn == "Motor" ||
        cn == "Attachment" || cn == "Force")                                 return ICON_CONSTRAINT;
    if (cn == "System") return ICON_SYSTEM;
    if (cn == "Weather") return ICON_WEATHER;
    if (cn == "StarterCharacter") return ICON_STARTERCHARACTER;
    if (cn == "AppImage") return ICON_APPIMAGE;
    if (cn == "PathfindingService") return ICON_PATHFINDINGSERVICE;
    if (cn == "PostEffect") return ICON_POSTEFFECT;
    if (cn == "IntValue" || cn == "BoolValue" || cn == "NumberValue" || cn == "Vector3Value" ||
        cn == "Color4Value" || cn == "CFrameValue" || cn == "QuaternionValue" || cn == "ObjectValue")
                                                                              return ICON_VALUE;
    if (cn == "Users") return ICON_USERS;
    if (cn == "ChatService") return ICON_CHATSERVICE;

    return ICON_INSTANCE;
}

void SceneHierarchyPanel::requestReveal(Instance* inst) {
    m_revealRequest = inst;
}

void SceneHierarchyPanel::drawNode(Instance* inst) {
    if (!inst) return;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    // The system root is the stable entry point for the editor hierarchy. Keep
    // only this root expanded by default so automation can discover Workspace;
    // users can still collapse it normally afterwards.
    if (inst == systemRoot || inst->IsA("Workspace"))
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool inSelection = std::find(selectedInstances.begin(), selectedInstances.end(), inst)
                       != selectedInstances.end();
    if (selectedInstance == inst || inSelection) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool isLeaf = inst->getChildren().empty();
    if (isLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // Ctrl+F リビール: 対象の祖先ノードを自動展開する
    if (m_revealRequest && m_revealRequest != inst) {
        for (auto p = m_revealRequest->Parent.lock(); p; p = p->Parent.lock()) {
            if (p.get() == inst) { ImGui::SetNextItemOpen(true); break; }
        }
    }

    if (inst == renamingInstance && inst->isRuntimeNameLocked()) {
        renamingInstance = nullptr;
        renameFocusPending = false;
    }
    bool renaming = !readOnly && !inst->isRuntimeNameLocked() && (inst == renamingInstance);
    bool open = renaming
        ? ImGui::TreeNodeEx(inst, flags, "%s", getClassIcon(inst->getClassName()))
        : ImGui::TreeNodeEx(inst, flags, "%s %s", getClassIcon(inst->getClassName()), inst->Name.c_str());
    GuiAutomation::registerLastItem(std::string("Explorer/Node/") + inst->getFullPath());

    if (inst == m_revealRequest) {
        ImGui::SetScrollHereY(0.5f);
        m_revealRequest = nullptr;
    }

    if (renaming) {
        ImGui::SameLine();
        if (renameFocusPending) {
            strncpy(m_renameBuf, inst->Name.c_str(), sizeof(m_renameBuf) - 1);
            ImGui::SetKeyboardFocusHere();
            renameFocusPending = false;
        }
        bool commit = ImGui::InputText("##rename", m_renameBuf, sizeof(m_renameBuf),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (!cancel && (commit || ImGui::IsItemDeactivated())) {
            std::string before = inst->Name;
            std::string after  = m_renameBuf;
            if (!after.empty() && after != before) {
                inst->renameTo(after);
                if (m_history)
                    m_history->record(std::make_unique<RenameInstanceCommand>(
                        inst->shared_from_this(), before, inst->Name));
            }
        }
        if (cancel || commit || ImGui::IsItemDeactivated()) {
            renamingInstance = nullptr;
        }
    }

    if (!renaming && ImGui::IsItemClicked()) {
        // ---- ピッカーモード: Pick 中はクリックを Cube/Attachment 参照指定に横取り（選択は変更しない） ----
        if (m_picker && m_picker->active) {
            const bool matches = !m_picker->pickClassName.empty()
                                ? inst->IsA(m_picker->pickClassName)
                                : m_picker->pickAnyInstance ? true
                                : m_picker->pickAttachment  ? inst->IsA("Attachment")
                                                             : inst->IsA("BaseCube");
            if (matches && inst != m_picker->constraint && m_picker->onPick)
                m_picker->onPick(inst->shared_from_this());
            m_picker->active = false;
        } else
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
        } else if (!inSelection) {
            selectedInstance = inst;
            selectedInstances = { inst };
        } else {
            // 既に複数選択に含まれる項目のプレーンクリックでは集合を潰さない
            // （まとめてドラッグできるように）。単一化はドラッグせず離したときに行う。
            selectedInstance = inst;
        }
    }

    // プレーンクリック（ドラッグせず離した）で複数選択を単一へ畳む
    if (!renaming && inSelection && selectedInstances.size() > 1
        && ImGui::IsItemHovered()
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && !ImGui::GetIO().KeyCtrl) {
        ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (dd.x * dd.x + dd.y * dd.y < 25.0f) {  // ほぼ動いていない＝クリック（ドラッグでない）
            selectedInstance = inst;
            selectedInstances = { inst };
        }
    }

    // ---- ドラッグソース ----
    if (!readOnly && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("INSTANCE_PTR", &inst, sizeof(Instance*));
        ImGui::Text("%s", inst->Name.c_str());
        ImGui::EndDragDropSource();
    }

    // ---- ドロップターゲット ----
    if (!readOnly && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INSTANCE_PTR")) {
            Instance* dragged = *static_cast<Instance* const*>(payload->Data);
            auto newParent = inst->shared_from_this();

            // 移動対象: dragged が複数選択に含まれるなら選択全体、そうでなければ dragged のみ
            std::vector<Instance*> movers;
            bool draggedInSel = std::find(selectedInstances.begin(), selectedInstances.end(), dragged)
                                != selectedInstances.end();
            if (draggedInSel && selectedInstances.size() > 1) movers = selectedInstances;
            else                                              movers.push_back(dragged);

            // target が x 自身か x の子孫か（自分/子孫へのドロップ禁止）
            auto isSelfOrDescendantOf = [](Instance* target, Instance* x) {
                for (Instance* c = target; c; ) {
                    if (c == x) return true;
                    auto p = c->Parent.lock(); c = p ? p.get() : nullptr;
                }
                return false;
            };
            // 祖先も movers に含まれる子孫は除外（親と一緒に動くため二重移動しない）
            auto ancestorInMovers = [&](Instance* x) {
                for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                    if (std::find(movers.begin(), movers.end(), p.get()) != movers.end()) return true;
                return false;
            };

            if (m_history) {
                // 走査中に children マップを変更しないよう、移動はここでは積むだけにして
                // drawNode 完了後（onRender）にまとめて実行する。
                bool queuedAny = false;
                for (Instance* d : movers) {
                    if (!d) continue;
                    if (isSelfOrDescendantOf(inst, d)) continue;             // 自分/子孫へは不可
                    if (movers.size() > 1 && ancestorInMovers(d)) continue;  // 親が一緒に動くものは除外
                    auto oldParent = d->Parent.lock();
                    if (!oldParent || oldParent == newParent) continue;      // 既に同じ親なら不要
                    auto it = oldParent->children.find(d->Name);
                    if (it == oldParent->children.end()) continue;
                    m_pendingReparents.push_back({ oldParent, newParent, it->second });
                    queuedAny = true;
                }
                if (queuedAny) m_pendingSelect = dragged;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ---- 右クリックコンテキストメニュー ----
    std::string popupId = "ctx##" + std::to_string(reinterpret_cast<uintptr_t>(inst));
    if (!readOnly && ImGui::BeginPopupContextItem(popupId.c_str())) {
        renderContextMenu(inst);
        ImGui::EndPopup();
    }

    if (!isLeaf && open) {
        std::vector<Instance*> children;
        children.reserve(inst->getChildren().size());
        for (auto const& [name, child] : inst->getChildren()) {
            if (child) children.push_back(child.get());
        }
        for (Instance* child : children) {
            drawNode(child);
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::requestNewScript(const std::shared_ptr<Instance>& parent) {
    if (!parent) return;
    m_pendingGroupTargets.clear();
    m_pendingScriptParent = parent;
    m_openScriptDialog     = true;
    m_pendingScriptClass   = ScriptInsertClass::Script;
    m_scriptDialogError.clear();
}

void SceneHierarchyPanel::requestNewTextFile(const std::shared_ptr<Instance>& parent) {
    if (!parent) return;
    m_pendingGroupTargets.clear();
    m_pendingTextFileParent = parent;
    m_openTextFileDialog = true;
    m_textFileDialogError.clear();
}

void SceneHierarchyPanel::renderTextFileDialog() {
    if (m_openTextFileDialog) {
        ImGui::OpenPopup("###TextFile");
        m_openTextFileDialog = false;
    }
    const std::string title = std::string(Loc::t(Loc::LocKey::TextFileTitle)) + "###TextFile";
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char name[128] = "NewTextFile";
        static int mode = 0;
        ImGui::RadioButton(Loc::t(Loc::LocKey::TextFileModeNew), &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton(Loc::t(Loc::LocKey::TextFileModeExisting), &mode, 1);
        ImGui::Separator();
        if (mode == 0) {
            ImGui::Text("%s", Loc::t(Loc::LocKey::TextFileNameLabel));
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##textfilename", name, sizeof(name));
        } else ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::TextFilePickHint));
        if (!m_textFileDialogError.empty())
            ImGui::TextColored(ImVec4(1, .35f, .35f, 1), "%s", m_textFileDialogError.c_str());
        if (ImGui::Button(Loc::t(Loc::LocKey::OK), ImVec2(100, 0))) {
            std::string requested(name);
            if (mode == 0 && requested.size() >= 4 && requested.substr(requested.size() - 4) == ".txt")
                requested.resize(requested.size() - 4);
            bool invalidName = requested.empty() || requested == "." || requested == ".." ||
                               requested.find_first_of("/\\:*?\"<>|") != std::string::npos;
            if (mode == 0 && invalidName)
                m_textFileDialogError = Loc::t(Loc::LocKey::TextFileNameError);
            else {
                m_textFileName = requested;
                m_pickExistingTextFile = mode == 1;
                m_doPickTextFile = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::Cancel), ImVec2(100, 0))) {
            m_pendingTextFileParent.reset();
            m_pendingGroupTargets.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!m_doPickTextFile) return;
    m_doPickTextFile = false;
    std::string path;
    bool retry = false;
    if (m_pickExistingTextFile) {
        path = getPlatform().openFileDialog({{"Text file (*.txt)", "*.txt"}});
    } else {
        const std::string folder = pickFolder();
        if (!folder.empty()) {
            std::filesystem::path target = std::filesystem::absolute(folder) / (m_textFileName + ".txt");
            std::ofstream out(target, std::ios::binary | std::ios::out | std::ios::noreplace);
            if (!out.is_open()) {
                m_textFileDialogError = Loc::t(Loc::LocKey::TextFileWriteError);
                m_openTextFileDialog = true;
                retry = true;
            } else {
                out.close();
                path = target.string();
            }
        }
    }
    if (!path.empty() && m_pendingTextFileParent && m_history) {
        auto file = std::make_shared<TextFile>();
        file->Path = textFileProjectRelative(path);
        std::filesystem::path selected(path);
        file->Name = uniqueName(m_pendingTextFileParent,
                                m_pickExistingTextFile ? selected.stem().string() : m_textFileName);
        getPlatform().revealInFileManager(path);
        if (!m_pendingGroupTargets.empty()) {
            m_history->execute(std::make_unique<GroupInstancesCommand>(
                m_pendingTextFileParent, file, m_pendingGroupTargets));
        } else {
            m_history->execute(std::make_unique<AddInstanceCommand>(m_pendingTextFileParent, file));
        }
        selectedInstance = file.get();
        selectedInstances = {file.get()};
    }
    if (!retry) {
        m_pendingTextFileParent.reset();
        m_pendingGroupTargets.clear();
        m_textFileDialogError.clear();
    }
}

void SceneHierarchyPanel::renderNewScriptDialog() {
    if (m_openScriptDialog) {
        ImGui::OpenPopup("###NewScript");
        m_openScriptDialog = false;
    }

    std::string popupTitle = std::string(Loc::t(Loc::LocKey::NewScriptTitle)) + "###NewScript";
    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char s_name[128] = "NewScript";
        static int  s_mode = 0; // 0=新規作成, 1=既存ファイル

        if (ImGui::RadioButton(Loc::t(Loc::LocKey::ScriptModeNew), &s_mode, 0)) {
            m_scriptDialogError.clear();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(Loc::t(Loc::LocKey::ScriptModeExisting), &s_mode, 1)) {
            m_scriptDialogError.clear();
        }
        ImGui::Separator();

        if (s_mode == 0) {
            ImGui::Text("%s", Loc::t(Loc::LocKey::ScriptNameLabel));
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("##sname", s_name, sizeof(s_name))) {
                m_scriptDialogError.clear();
            }
        } else {
            ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::ScriptPickHint));
        }

        if (!m_scriptDialogError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_scriptDialogError.c_str());
        }

        if (ImGui::Button(Loc::t(Loc::LocKey::OK), ImVec2(100, 0))) {
            std::string requestedName(s_name);
            if (s_mode == 0 && !isValidNewScriptName(requestedName)) {
                m_scriptDialogError = SCRIPT_NAME_ERROR;
            } else {
                m_scriptDialogError.clear();
                m_pickName     = requestedName;
                m_pickParent   = m_pendingScriptParent;
                m_pickExisting = (s_mode == 1);
                m_doPick       = true;
                m_pendingScriptParent.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::Cancel), ImVec2(100, 0))) {
            m_pendingScriptParent.reset();
            m_pendingGroupTargets.clear();
            m_scriptDialogError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // ポップアップが閉じた後にフォルダ/ファイル選択を実行
    if (m_doPick) {
        m_doPick = false;
        std::string filePath;
        bool reopenDialog = false;

        auto reopenWithError = [this, &reopenDialog](const char* message) {
            m_scriptDialogError = message;
            m_pendingScriptParent = m_pickParent;
            m_openScriptDialog = true;
            reopenDialog = true;
        };

        if (m_pickExisting) {
            filePath = pickFile();
            if (!filePath.empty()) {
                std::error_code ec;
                std::filesystem::path absolutePath =
                    std::filesystem::absolute(std::filesystem::path(filePath), ec);
                if (!ec) filePath = absolutePath.string();
            }
        } else {
            std::string folder = pickFolder();
            if (!folder.empty()) {
                std::error_code ec;
                std::filesystem::path folderPath =
                    std::filesystem::absolute(std::filesystem::path(folder), ec);
                if (ec) {
                    reopenWithError(SCRIPT_PATH_ERROR);
                } else {
                    std::filesystem::path scriptPath = folderPath / (m_pickName + ".luau");
                    bool scriptExists = std::filesystem::exists(scriptPath, ec);
                    if (ec) {
                        reopenWithError(SCRIPT_PATH_ERROR);
                    } else if (scriptExists) {
                        reopenWithError(SCRIPT_EXISTS_ERROR);
                    } else {
                        std::ofstream stream(scriptPath, std::ios::out | std::ios::noreplace);
                        bool fileCreated = stream.is_open();
                        bool writeSucceeded = fileCreated;
                        if (writeSucceeded) {
                            if (m_pendingScriptClass == ScriptInsertClass::ModuleScript) {
                                // requireは返り値を要求するため、モジュールの雛形を書いておく
                                stream << "-- " << m_pickName << "\nlocal M = {}\n\nreturn M\n";
                            } else {
                                stream << "-- " << m_pickName << "\n";
                            }
                            writeSucceeded = stream.good();
                            stream.close();
                            writeSucceeded = writeSucceeded && !stream.fail();
                        }

                        if (!writeSucceeded) {
                            if (fileCreated) {
                                std::error_code removeError;
                                std::filesystem::remove(scriptPath, removeError);
                            }
                            reopenWithError(SCRIPT_WRITE_ERROR);
                        } else {
                            filePath = scriptPath.string();
                        }
                    }
                }
            }
        }

        if (!filePath.empty() && m_pickParent && m_history) {
            getPlatform().revealInFileManager(filePath);

            // 既存選択時はファイル名をスクリプト名にする
            if (m_pickExisting) {
                std::filesystem::path selectedPath(filePath);
                std::string fileName = selectedPath.filename().string();
                bool isLuar = selectedPath.extension() == ".luar";
                m_pickName = (selectedPath.extension().empty() || isLuar)
                    ? fileName
                    : selectedPath.stem().string();
            }

            std::shared_ptr<Script> script;
            switch (m_pendingScriptClass) {
                case ScriptInsertClass::LocalScript:  script = std::make_shared<LocalScript>(filePath);  break;
                case ScriptInsertClass::ModuleScript: script = std::make_shared<ModuleScript>(filePath); break;
                default:                              script = std::make_shared<Script>(filePath);       break;
            }
            script->Name = m_pickName;
            if (!m_pendingGroupTargets.empty()) {
                auto groupParent = m_pendingGroupTargets.front()->Parent.lock();
                if (groupParent) {
                    m_history->execute(std::make_unique<GroupInstancesCommand>(
                        groupParent, script, m_pendingGroupTargets));
                    selectedInstances = { script.get() };
                    selectedInstance = script.get();
                }
                m_pendingGroupTargets.clear();
            } else {
                m_history->execute(std::make_unique<AddInstanceCommand>(m_pickParent, script));
            }
        }
        m_pickParent.reset();
        if (filePath.empty() && !reopenDialog) m_pendingGroupTargets.clear();
        if (!reopenDialog) m_scriptDialogError.clear();
    }
}

void SceneHierarchyPanel::renderNewTerrainDialog() {
    if (m_openTerrainDialog) {
        ImGui::OpenPopup("###NewTerrain");
        m_openTerrainDialog = false;
    }
    if (ImGui::BeginPopupModal(
            "Terrain Data###NewTerrain", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        static char terrainName[128] = "Terrain";
        static int mode = 0;
        ImGui::RadioButton("Create new", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Use existing", &mode, 1);
        ImGui::Separator();
        if (mode == 0) {
            ImGui::TextUnformatted("Terrain name");
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("##terrainName", terrainName, sizeof(terrainName));
            ImGui::TextDisabled("A parent folder will be selected next.");
        } else {
            ImGui::TextDisabled("Select an existing Terrain region directory.");
        }
        if (ImGui::Button("OK", ImVec2(100, 0))) {
            m_pendingTerrainName = terrainName;
            m_pickExistingTerrain = mode == 1;
            m_doPickTerrain = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pendingTerrainParent.reset();
            m_pendingGroupTargets.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!m_doPickTerrain) return;
    m_doPickTerrain = false;
    const std::string selected = pickFolder();
    if (selected.empty() || !m_pendingTerrainParent || !m_history) {
        m_pendingTerrainParent.reset();
        m_pendingGroupTargets.clear();
        return;
    }

    std::filesystem::path dataPath(selected);
    if (!m_pickExistingTerrain) {
        if (m_pendingTerrainName.empty()) {
            RCBN_WARN("Terrain creation requires a non-empty name");
            m_pendingTerrainParent.reset();
            m_pendingGroupTargets.clear();
            return;
        }
        dataPath /= m_pendingTerrainName;
        std::error_code error;
        if (std::filesystem::exists(dataPath, error) &&
            !std::filesystem::is_empty(dataPath, error)) {
            RCBN_WARN("Terrain directory is not empty; refusing to overwrite: "
                      << dataPath.string());
            m_pendingTerrainParent.reset();
            m_pendingGroupTargets.clear();
            return;
        }
        if (!std::filesystem::create_directories(dataPath, error) && error) {
            RCBN_WARN("Failed to create Terrain directory: " << error.message());
            m_pendingTerrainParent.reset();
            m_pendingGroupTargets.clear();
            return;
        }
    }

    auto terrain = std::make_shared<Terrain>();
    terrain->Name = uniqueName(
        m_pendingTerrainParent,
        m_pendingTerrainName.empty() ? "Terrain" : m_pendingTerrainName);
    terrain->setDataPath(dataPath.string());
    if (!m_pendingGroupTargets.empty()) {
        auto groupParent = m_pendingGroupTargets.front()->Parent.lock();
        if (groupParent) {
            m_history->execute(std::make_unique<GroupInstancesCommand>(
                groupParent, terrain, m_pendingGroupTargets));
            selectedInstances = { terrain.get() };
            selectedInstance = terrain.get();
        }
        m_pendingGroupTargets.clear();
    } else {
        m_history->execute(std::make_unique<AddInstanceCommand>(
            m_pendingTerrainParent, terrain));
    }
    m_pendingTerrainParent.reset();
}

void SceneHierarchyPanel::openClassPicker(ClassPickerMode mode, Instance* inst) {
    if (!inst) return;
    m_classPickerMode = mode;
    m_classPickerParent.reset();
    m_classPickerTarget.reset();
    m_classPickerTargets.clear();
    if (mode == ClassPickerMode::Replace) {
        if (inst == systemRoot || inst == workspace || !inst->Parent.lock()) return;
        m_classPickerTarget = inst->shared_from_this();
    } else if (mode == ClassPickerMode::Insert) {
        m_classPickerParent = inst->shared_from_this();
    } else {
        bool inSelection = std::find(selectedInstances.begin(), selectedInstances.end(), inst)
                            != selectedInstances.end();
        const auto raw = (inSelection && selectedInstances.size() > 1)
            ? selectedInstances : std::vector<Instance*>{inst};
        for (Instance* target : raw) {
            if (!target || target == systemRoot || target == workspace) continue;
            auto parent = target->Parent.lock();
            if (!parent) continue;
            bool nested = false;
            for (auto p = target->Parent.lock(); p; p = p->Parent.lock())
                if (std::find(raw.begin(), raw.end(), p.get()) != raw.end()) { nested = true; break; }
            if (!nested) m_classPickerTargets.push_back(target->shared_from_this());
        }
        if (m_classPickerTargets.empty()) return;
    }
    std::fill(std::begin(m_classPickerSearch), std::end(m_classPickerSearch), '\0');
    m_classPickerSelected.clear();
    m_classPickerConfirmClass.clear();
    m_classPickerOpen = true;
    m_classPickerPopupRequested = true;
}

void SceneHierarchyPanel::renderClassPicker() {
    // Keep the popup lifecycle entry point small.  Rendering and action
    // handling live in a dedicated routine so callers cannot accidentally
    // couple picker state management to the surrounding hierarchy UI.
    renderClassPickerPopup();
}

void SceneHierarchyPanel::renderClassPickerPopup() {
    if (!m_classPickerOpen) return;
    bool popupRequestedThisFrame = false;
    if (m_classPickerPopupRequested) {
        const ImVec2 parentPos = ImGui::GetWindowPos();
        const ImVec2 parentSize = ImGui::GetWindowSize();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        const ImVec2 workPos = mainViewport->WorkPos;
        const ImVec2 workSize = mainViewport->WorkSize;
        constexpr float pickerWidth = 400.0f;
        constexpr float pickerHeight = 520.0f;
        float x = parentPos.x + parentSize.x + 6.0f;
        if (x + pickerWidth > workPos.x + workSize.x)
            x = parentPos.x - pickerWidth - 6.0f;
        x = std::max(workPos.x, std::min(x, workPos.x + workSize.x - pickerWidth));
        const float y = std::max(workPos.y, std::min(parentPos.y,
            workPos.y + workSize.y - pickerHeight));
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Appearing);
        ImGui::OpenPopup("###InstanceClassPicker");
        m_classPickerPopupRequested = false;
        popupRequestedThisFrame = true;
    }
    if (!ImGui::BeginPopup("###InstanceClassPicker", ImGuiWindowFlags_AlwaysAutoResize)) {
        // OpenPopup is queued by Dear ImGui and may only become visible on the
        // next frame. Keep the picker state alive during that transition; the
        // previous code cleared it immediately and made the child popup vanish
        // after the Insert/Group/Replace row was clicked.
        if (!popupRequestedThisFrame && !ImGui::IsPopupOpen("###InstanceClassPicker")) {
            m_classPickerOpen = false;
            m_classPickerConfirmClass.clear();
            m_classPickerSelected.clear();
        }
        return;
    }
    ImGui::Text("%s", Loc::t(Loc::LocKey::InstancePickerSearch));
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##class-search", m_classPickerSearch, sizeof(m_classPickerSearch));
    GuiAutomation::registerLastItem("Explorer/ClassPicker/Search");
    const auto matches = InstanceCatalog::search(m_classPickerSearch);
    bool activateSelection = renderClassPickerEntries(matches);
    const bool hasIncompatibleReferences = previewClassPickerSelection();
    renderClassPickerReplacementConfirmation(hasIncompatibleReferences, activateSelection);
    if (matches.empty()) ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::InstancePickerNoResults));
    ImGui::Separator();
    const bool canConfirm = !m_classPickerSelected.empty();
    if (activateSelection && canConfirm &&
        (!hasIncompatibleReferences || m_classPickerConfirmClass == m_classPickerSelected)) {
        executeClassPickerSelection(m_classPickerSelected);
        ImGui::CloseCurrentPopup(); m_classPickerOpen = false; m_classPickerCloseParent = true;
    }
    ImGui::EndPopup();
}

bool SceneHierarchyPanel::executeClassPickerSelection(const std::string& selected) {
    if (m_classPickerMode == ClassPickerMode::Insert && m_classPickerParent) {
        if (selected == "Script" || selected == "LocalScript" || selected == "ModuleScript") {
            m_pendingScriptParent = m_classPickerParent;
            m_pendingScriptClass = selected == "Script" ? ScriptInsertClass::Script
                : (selected == "LocalScript" ? ScriptInsertClass::LocalScript : ScriptInsertClass::ModuleScript);
            m_scriptDialogError.clear();
            m_openScriptDialog = true;
            return true;
        }
        if (selected == "TextFile") {
            requestNewTextFile(m_classPickerParent);
            return true;
        }
        if (selected == "Terrain") {
            m_pendingTerrainParent = m_classPickerParent;
            m_openTerrainDialog = true;
            return true;
        }
        if (!m_history) return false;
        auto obj = InstanceCatalog::create(selected);
        if (obj && obj->IsA("Spatial"))
            static_cast<Spatial*>(obj.get())->cframe.Position = computeSpawnPos(m_user, workspace);
        auto insertParent = m_classPickerParent;
        if (selected == "SurfaceMark" && insertParent->IsA("BaseCube") && workspace)
            insertParent = workspace->shared_from_this();
        if (selected == "Sound" && !AudioService::instance) obj.reset();
        if (selected == "Sun" && m_user && obj) {
            auto* sun = static_cast<Sun*>(obj.get());
            float rad = sun->Angle * (3.14159265f / 180.0f);
            sun->cframe.Position = m_user->cpos + Vector3(0.0f, std::sin(rad), std::cos(rad)) * 1000.0f;
        }
        if (selected == "Moon" && m_user && obj) {
            auto* moon = static_cast<Moon*>(obj.get());
            float rad = 45.0f * (3.14159265f / 180.0f);
            moon->cframe.Position = m_user->cpos - Vector3(0.0f, std::sin(rad), std::cos(rad)) * 1000.0f;
        }
        if (!obj) return false;
        obj->Name = uniqueName(insertParent, selected);
        m_history->execute(std::make_unique<AddInstanceCommand>(insertParent, obj));
        return true;
    }
    if (m_classPickerMode == ClassPickerMode::Group && m_history && !m_classPickerTargets.empty()) {
        auto parent = m_classPickerTargets.front()->Parent.lock();
        if (!parent) return false;
        if (selected == "Script" || selected == "LocalScript" || selected == "ModuleScript") {
            m_pendingGroupTargets = m_classPickerTargets;
            m_pendingScriptParent = parent;
            m_pendingScriptClass = selected == "Script" ? ScriptInsertClass::Script
                : (selected == "LocalScript" ? ScriptInsertClass::LocalScript : ScriptInsertClass::ModuleScript);
            m_scriptDialogError.clear();
            m_openScriptDialog = true;
        } else if (selected == "Terrain") {
            m_pendingGroupTargets = m_classPickerTargets;
            m_pendingTerrainParent = parent;
            m_pendingTerrainName = "Terrain";
            m_openTerrainDialog = true;
        } else if (selected == "TextFile") {
            m_pendingGroupTargets = m_classPickerTargets;
            m_pendingTextFileParent = parent;
            m_openTextFileDialog = true;
        } else {
            auto group = InstanceCatalog::create(selected);
            if (!group) return false;
            group->Name = uniqueName(parent, selected);
            m_history->execute(std::make_unique<GroupInstancesCommand>(parent, group, m_classPickerTargets));
            selectedInstance = group.get();
            selectedInstances = {group.get()};
        }
        return true;
    }
    if (m_classPickerMode == ClassPickerMode::Replace && m_history && m_classPickerTarget) {
        auto parent = m_classPickerTarget->Parent.lock();
        if (!parent || selected == m_classPickerTarget->getClassName()) return false;
        m_history->execute(std::make_unique<ReplaceInstanceCommand>(
            parent, m_classPickerTarget, selected,
            [this](Instance* replacement) { selectedInstance = replacement; selectedInstances = {replacement}; },
            systemRoot ? systemRoot->shared_from_this()
                       : (workspace ? workspace->shared_from_this() : nullptr)));
        return true;
    }
    return false;
}

bool SceneHierarchyPanel::renderClassPickerReplacementConfirmation(
    bool hasIncompatibleReferences, bool& activateSelection) {
    const bool acknowledged = !m_classPickerSelected.empty() &&
        m_classPickerConfirmClass == m_classPickerSelected;
    if (activateSelection && hasIncompatibleReferences && !acknowledged) {
        m_classPickerConfirmClass.clear();
        activateSelection = false;
    }
    if (hasIncompatibleReferences && !acknowledged) {
        const bool confirmClicked = ImGui::Button(
            Loc::t(Loc::LocKey::InstancePickerReplaceAndClear));
        GuiAutomation::registerLastItem("Explorer/ClassPicker/ConfirmReplace");
        if (confirmClicked) {
            m_classPickerConfirmClass = m_classPickerSelected;
            activateSelection = true;
        }
        ImGui::SameLine();
        const bool cancelClicked = ImGui::Button(Loc::t(Loc::LocKey::InstancePickerCancel));
        GuiAutomation::registerLastItem("Explorer/ClassPicker/CancelReplace");
        if (cancelClicked) {
            m_classPickerConfirmClass.clear();
            m_classPickerSelected.clear();
        }
    }
    // Re-read state so a confirmation click can complete replacement this
    // frame rather than waiting for another selection event.
    return !m_classPickerSelected.empty() &&
        m_classPickerConfirmClass == m_classPickerSelected;
}

bool SceneHierarchyPanel::renderClassPickerEntries(
    const std::vector<InstanceCatalogEntry>& matches) {
    constexpr InstanceCategory categories[] = { InstanceCategory::Cubes, InstanceCategory::Effects,
        InstanceCategory::Environment, InstanceCategory::Gui, InstanceCategory::Physics,
        InstanceCategory::Values, InstanceCategory::Container, InstanceCategory::File,
        InstanceCategory::Script, InstanceCategory::Other };
    auto categoryName = [](InstanceCategory category) {
        switch (category) {
        case InstanceCategory::Cubes: return Loc::t(Loc::LocKey::CategoryCubes);
        case InstanceCategory::Effects: return Loc::t(Loc::LocKey::CategoryEffects);
        case InstanceCategory::Environment: return Loc::t(Loc::LocKey::CategoryEnvironment);
        case InstanceCategory::Gui: return Loc::t(Loc::LocKey::CategoryGui);
        case InstanceCategory::Physics: return Loc::t(Loc::LocKey::CategoryPhysicsConstraints);
        case InstanceCategory::Values: return Loc::t(Loc::LocKey::CategoryValue);
        case InstanceCategory::Container: return Loc::t(Loc::LocKey::CategoryContainer);
        case InstanceCategory::File: return Loc::t(Loc::LocKey::CategoryFile);
        case InstanceCategory::Script: return Loc::t(Loc::LocKey::CategoryScript);
        default: return Loc::t(Loc::LocKey::CategoryOther);
        }
    };
    auto categoryDescription = [](InstanceCategory category) {
        switch (category) {
        case InstanceCategory::Cubes: return Loc::t(Loc::LocKey::CategoryCubesDesc);
        case InstanceCategory::Effects: return Loc::t(Loc::LocKey::CategoryEffectsDesc);
        case InstanceCategory::Environment: return Loc::t(Loc::LocKey::CategoryEnvironmentDesc);
        case InstanceCategory::Gui: return Loc::t(Loc::LocKey::CategoryGuiDesc);
        case InstanceCategory::Physics: return Loc::t(Loc::LocKey::CategoryPhysicsConstraintsDesc);
        case InstanceCategory::Values: return Loc::t(Loc::LocKey::CategoryValueDesc);
        case InstanceCategory::Container: return Loc::t(Loc::LocKey::CategoryContainerDesc);
        case InstanceCategory::File: return Loc::t(Loc::LocKey::CategoryFileDesc);
        case InstanceCategory::Script: return Loc::t(Loc::LocKey::CategoryScriptDesc);
        default: return Loc::t(Loc::LocKey::CategoryOtherDesc);
        }
    };
    bool activate = false;
    for (const auto category : categories) {
        bool hasMatch = false;
        for (const auto& entry : matches)
            if (entry.category == category) { hasMatch = true; break; }
        if (!hasMatch) continue;
        const bool expanded = m_classPickerSearch[0] != '\0';
        ImGui::PushID(static_cast<int>(category));
        if (expanded) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const char* categoryKey = InstanceCatalog::categoryLabel(category);
        ImGui::PushID(categoryKey);
        const bool categoryOpen = ImGui::CollapsingHeader(
            categoryName(category), expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        GuiAutomation::registerLastItem(std::string("Explorer/ClassPicker/Category/") + categoryKey);
        if (categoryOpen) {
            ImGui::TextDisabled("%s", categoryDescription(category));
            ImGui::Separator();
            ImGui::PushID("ClassRows");
            for (const auto& entry : matches) {
                if (entry.category != category) continue;
                const bool sameReplace = m_classPickerMode == ClassPickerMode::Replace &&
                    m_classPickerTarget && entry.className == m_classPickerTarget->getClassName();
                if (ImGui::Selectable(entry.className.data(), m_classPickerSelected == entry.className,
                                      ImGuiSelectableFlags_NoAutoClosePopups |
                                          (sameReplace ? ImGuiSelectableFlags_Disabled : 0))) {
                    m_classPickerSelected = std::string(entry.className);
                    m_classPickerConfirmClass.clear();
                    activate = true;
                }
                GuiAutomation::registerLastItem(std::string("Explorer/ClassPicker/Class/") +
                                                std::string(entry.className));
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    return activate;
}

bool SceneHierarchyPanel::previewClassPickerSelection() {
    if (m_classPickerMode != ClassPickerMode::Replace || !m_classPickerTarget ||
        m_classPickerSelected.empty()) return false;
    auto parent = m_classPickerTarget->Parent.lock();
    auto root = systemRoot ? systemRoot->shared_from_this()
                           : (workspace ? workspace->shared_from_this() : nullptr);
    auto previewReplacement = InstanceCatalog::create(m_classPickerSelected);
    ReplaceInstanceCommand preview(parent, m_classPickerTarget, m_classPickerSelected, {}, root);
    if (previewReplacement) preview.analyzeReferences(previewReplacement);
    if (preview.incompatibleReferenceOwners().empty()) return false;
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                       Loc::t(Loc::LocKey::InstancePickerIncompatibleCount),
                       static_cast<int>(preview.incompatibleReferenceOwners().size()));
    for (const auto& owner : preview.incompatibleReferenceOwners())
        ImGui::BulletText("%s", owner.c_str());
    return true;
}

#if 0 // Legacy hierarchical insert menu retained only as historical reference.
void SceneHierarchyPanel::renderInsertMenu(Instance* inst) {
    auto parentSp = inst->shared_from_this();

    // ---- Cube系 ----
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryCubes))) {
        auto spawnPos = computeSpawnPos(m_user, workspace);

        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryCubesDesc));
        ImGui::Separator();
        tryInsertInstance<Cube>(m_history, "Cube", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Cylinder>(m_history, "Cylinder", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<TriangularPrism>(m_history, "TriangularPrism", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<Truss>(m_history, "Truss", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Seat>(m_history, "Seat", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Sphere>(m_history, "Sphere", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<MeshCube>(m_history, "MeshCube", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<LiquidCube>(m_history, "LiquidCube", parentSp, spawnPos, Vector3(4, 2, 4));
        tryInsertInstance<SpawnLocation>(m_history, "SpawnLocation", parentSp, spawnPos);

        ImGui::EndMenu();
    }

    // ---- 効果 ----
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryEffects))) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryEffectsDesc));
        ImGui::Separator();
        if (ImGui::MenuItem("Sound", nullptr, false, AudioService::instance != nullptr) && m_history) {
            auto obj = std::make_shared<Sound>(*AudioService::instance);
            obj->Name = uniqueName(parentSp, "Sound");
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (AudioService::instance == nullptr) {
            ImGui::SetItemTooltip("%s", Loc::t(Loc::LocKey::AudioServiceUnavailable));
        }
        tryInsertInstance<Decal>(m_history, "Decal", parentSp, 0, Face::Front);
        tryInsertInstance<Texture>(m_history, "Texture", parentSp, 0, Face::Front);
        if (ImGui::MenuItem("SurfaceMark") && m_history) {
            // SurfaceMark は BaseCube 所属ではなく、空間上の独立インスタンスとして配置する。
            auto surfaceParent = parentSp;
            if (surfaceParent->IsA("BaseCube") && workspace)
                surfaceParent = workspace->shared_from_this();
            auto mark = std::make_shared<SurfaceMark>(computeSpawnPos(m_user, workspace), Vector3(4, 4, 4));
            mark->Name = uniqueName(surfaceParent, "SurfaceMark");
            m_history->execute(std::make_unique<AddInstanceCommand>(surfaceParent, mark));
        }
        tryInsertInstance<PostEffect>(m_history, "PostEffect", parentSp);
        tryInsertInstance<ParticleEmitter>(m_history, "ParticleEmitter", parentSp);
        tryInsertInstance<Highlight>(m_history, "Highlight", parentSp);

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryEnvironment))) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryEnvironmentDesc));
        ImGui::Separator();

        tryInsertInstance<Workspace>(m_history, "Workspace", parentSp);
        tryInsertInstance<Weather>(m_history, "Weather", parentSp);
        if (ImGui::MenuItem("Terrain") && m_history) {
            m_pendingTerrainParent = parentSp;
            m_openTerrainDialog = true;
        }
        tryInsertInstance<Skybox>(m_history, "Skybox", parentSp);

        tryInsertInstance<Lighting>(m_history, "Lighting", parentSp);
        tryInsertInstance<PointLight>(m_history, "PointLight", parentSp);
        tryInsertInstance<SpotLight>(m_history, "SpotLight", parentSp);

        // Sun / Moon は追加した瞬間にカメラ基準の空座標を計算する
        // （Renderer のフォーカス時追従に頼ると、追加直後は原点に出てバグに見えるため）
        if (ImGui::MenuItem("Sun") && m_history) {
            auto obj = std::make_shared<Sun>();
            obj->Name = uniqueName(parentSp, "Sun");
            if (m_user) {
                float rad = obj->Angle * (3.14159265f / 180.0f);
                Vector3 dir(0.0f, std::sin(rad), std::cos(rad));
                obj->cframe.Position = m_user->cpos + dir * 1000.0f;
            }
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        if (ImGui::MenuItem("Moon") && m_history) {
            auto obj = std::make_shared<Moon>();
            obj->Name = uniqueName(parentSp, "Moon");
            if (m_user) {
                // 既存の Sun があればその反対側、無ければ既定角の反対側に置く
                float angle = 45.0f;
                for (auto const& [n, c] : parentSp->children) {
                    if (c->IsA("Sun")) { angle = static_cast<Sun*>(c.get())->Angle; break; }
                }
                float rad = angle * (3.14159265f / 180.0f);
                Vector3 dir(0.0f, std::sin(rad), std::cos(rad));
                obj->cframe.Position = m_user->cpos - dir * 1000.0f;
            }
            m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
        
        ImGui::EndMenu();
    }

    // ---- その他 ----
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryOther))) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryOtherDesc));
        ImGui::Separator();

        // Script系はダイアログを開く特殊な挙動なのでそのまま
        if (ImGui::MenuItem("Script")) {
            m_pendingScriptParent = parentSp;
            m_openScriptDialog    = true;
            m_pendingScriptClass  = ScriptInsertClass::Script;
            m_scriptDialogError.clear();
        }
        if (ImGui::MenuItem("LocalScript")) {
            m_pendingScriptParent = parentSp;
            m_openScriptDialog    = true;
            m_pendingScriptClass  = ScriptInsertClass::LocalScript;
            m_scriptDialogError.clear();
        }
        if (ImGui::MenuItem("ModuleScript")) {
            m_pendingScriptParent = parentSp;
            m_openScriptDialog    = true;
            m_pendingScriptClass  = ScriptInsertClass::ModuleScript;
            m_scriptDialogError.clear();
        }
        
        tryInsertInstance<Folder>(m_history, "Folder", parentSp);
        if (ImGui::MenuItem("TextFile") && m_history)
            requestNewTextFile(parentSp);
        for (const auto& type : PhysicalFileInstanceRegistry::types()) {
            if (type.insertCategory != PhysicalFileInsertCategory::Other) continue;
            if (std::string(type.className) == "TextFile") continue;
            const std::string className(type.className);
            if (ImGui::MenuItem(className.c_str()) && m_history) {
                auto obj = PhysicalFileInstanceRegistry::create(type.className);
                if (!obj) continue;
                obj->Name = uniqueName(parentSp, className);
                m_history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
            }
        }
        tryInsertInstance<Model>(m_history, "Model", parentSp, Vector3(0, 0, 0), Vector3(1, 1, 1));
        tryInsertInstance<Tool>(m_history, "Tool", parentSp, std::string("Tool"));



        tryInsertInstance<AppImage>(m_history, "AppImage", parentSp);
        tryInsertInstance<StarterCharacter>(m_history, "StarterCharacter", parentSp);
        tryInsertInstance<Humanoid>(m_history, "Humanoid", parentSp);
        tryInsertInstance<Animation>(m_history, "Animation", parentSp);
        tryInsertInstance<SignalEvent>(m_history, "SignalEvent", parentSp);

        ImGui::EndMenu();
    }

    // ---- GUI ----
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryGui))) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryGuiDesc));
        ImGui::Separator();
        tryInsertInstance<TextLabel>(m_history, "TextLabel", parentSp);
        tryInsertInstance<TextButton>(m_history, "TextButton", parentSp);
        tryInsertInstance<SurfaceGui>(m_history, "SurfaceGui", parentSp);
        tryInsertInstance<Canvas>(m_history, "Canvas", parentSp);
        tryInsertInstance<BillboardGui>(m_history, "BillboardGui", parentSp);
        tryInsertInstance<ProximityPrompt>(m_history, "ProximityPrompt", parentSp);
        tryInsertInstance<ImageLabel>(m_history, "ImageLabel", parentSp);
        tryInsertInstance<ImageButton>(m_history, "ImageButton", parentSp);

        ImGui::EndMenu();
    }

    // ---- 物理制約 ----
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryPhysicsConstraints))) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryPhysicsConstraintsDesc));
        ImGui::Separator();
        
        tryInsertInstance<Weld>(m_history, "Weld", parentSp);
        tryInsertInstance<Motor>(m_history, "Motor", parentSp);
        tryInsertInstance<Rod>(m_history, "Rod", parentSp);
        tryInsertInstance<BallSocket>(m_history, "BallSocket", parentSp);
        tryInsertInstance<NoCollision>(m_history, "NoCollision", parentSp);
        tryInsertInstance<Rope>(m_history, "Rope", parentSp);
        tryInsertInstance<Attachment>(m_history, "Attachment", parentSp);
        tryInsertInstance<Force>(m_history, "Force", parentSp);

        ImGui::EndMenu();
    }

    // ---- 値系インスタンス ----
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryValue))) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::CategoryValueDesc));
        ImGui::Separator();

        tryInsertInstance<IntValue>(m_history, "IntValue", parentSp);
        tryInsertInstance<BoolValue>(m_history, "BoolValue", parentSp);
        tryInsertInstance<NumberValue>(m_history, "NumberValue", parentSp);
        tryInsertInstance<Vector3Value>(m_history, "Vector3Value", parentSp);
        tryInsertInstance<Color4Value>(m_history, "Color4Value", parentSp);
        tryInsertInstance<CFrameValue>(m_history, "CFrameValue", parentSp);
        tryInsertInstance<QuaternionValue>(m_history, "QuaternionValue", parentSp);
        tryInsertInstance<ObjectValue>(m_history, "ObjectValue", parentSp);

        ImGui::EndMenu();
    }
}

#endif

void SceneHierarchyPanel::renderContextMenu(Instance* inst) {
    if (!inst) return;

    // ---- Workspace 専用ボタン ----
    if (inst->IsA("Workspace")) {
        auto* ws = static_cast<Workspace*>(inst);
        if (ImGui::MenuItem(Loc::t(Loc::LocKey::SwitchToWorkspace)) && onSwitchWorkspace) {
            onSwitchWorkspace(ws);
        }
        if (ImGui::MenuItem(Loc::t(Loc::LocKey::OpenInNewViewport)) && onOpenSecondaryViewport) {
            onOpenSecondaryViewport(ws);
        }
        ImGui::Separator();
    }

    const bool insertClicked = ImGui::Selectable(Loc::t(Loc::LocKey::InsertObjectMenu), false, ImGuiSelectableFlags_DontClosePopups);
    GuiAutomation::registerLastItem("Explorer/Context/InsertObject");
    if (insertClicked)
        openClassPicker(ClassPickerMode::Insert, inst);

    const bool canReplace = inst != systemRoot && inst != workspace && inst->Parent.lock();
    if (!canReplace) ImGui::BeginDisabled();
    const bool replaceClicked = ImGui::Selectable(Loc::t(Loc::LocKey::InstancePickerTitleReplace), false,
                          ImGuiSelectableFlags_DontClosePopups);
    GuiAutomation::registerLastItem("Explorer/Context/ReplaceInstance");
    if (replaceClicked)
        openClassPicker(ClassPickerMode::Replace, inst);
    if (!canReplace) ImGui::EndDisabled();

    // Group selected nodes into a newly-created container.  A right-click on a
    // selected node applies to the whole selection; otherwise it applies only
    // to the clicked node (matching Delete/Copy behavior below).
    const bool groupClicked = ImGui::Selectable(Loc::t(Loc::LocKey::MenuGroup), false, ImGuiSelectableFlags_DontClosePopups);
    GuiAutomation::registerLastItem("Explorer/Context/Group");
    if (groupClicked && m_history)
        openClassPicker(ClassPickerMode::Group, inst);
#if 0 // Legacy hierarchical grouping menu.
    if (ImGui::BeginMenu(Loc::t(Loc::LocKey::MenuGroup)) && m_history) {
        bool inSelection = std::find(selectedInstances.begin(), selectedInstances.end(), inst)
                           != selectedInstances.end();
        std::vector<Instance*> rawTargets =
            (inSelection && selectedInstances.size() > 1) ? selectedInstances
                                                           : std::vector<Instance*>{inst};
        std::vector<std::shared_ptr<Instance>> targets;
        for (Instance* target : rawTargets) {
            if (!target || target == systemRoot) continue;
            auto parent = target->Parent.lock();
            if (!parent) continue;
            // Never include a descendant when one of its ancestors is selected.
            bool ancestorSelected = false;
            for (auto p = target->Parent.lock(); p; p = p->Parent.lock()) {
                if (std::find(rawTargets.begin(), rawTargets.end(), p.get()) != rawTargets.end()) {
                    ancestorSelected = true; break;
                }
            }
            if (!ancestorSelected) targets.push_back(target->shared_from_this());
        }
        auto makeGroup = [&](const char* base, auto factory) {
            if (!ImGui::MenuItem(base) || targets.empty()) return;
            auto parent = targets.front()->Parent.lock();
            if (!parent) return;
            for (auto p = parent; p; p = p->Parent.lock()) {
                if (std::find_if(targets.begin(), targets.end(), [&](const auto& t) {
                        return t.get() == p.get(); }) != targets.end()) return;
            }
            auto group = factory();
            group->Name = uniqueName(parent, base);
            m_history->execute(std::make_unique<GroupInstancesCommand>(
                parent, group, targets));
            selectedInstances = { group.get() };
            selectedInstance = group.get();
        };
        makeGroup("Model", [&] { return std::make_shared<Model>(Vector3(0, 0, 0), Vector3(1, 1, 1)); });
        makeGroup("Folder", [&] { return std::make_shared<Folder>(); });
        makeGroup("Tool", [&] { return std::make_shared<Tool>(std::string("Tool")); });
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryOther))) {
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryCubes))) {
            makeGroup("Cube", [&] { return std::make_shared<Cube>(Vector3(), Vector3(1,1,1), Cube::defaultTextureID); });
            makeGroup("Cylinder", [&] { return std::make_shared<Cylinder>(Vector3(), Vector3(1,1,1)); });
            makeGroup("TriangularPrism", [&] { return std::make_shared<TriangularPrism>(Vector3(), Vector3(1,1,1)); });
            makeGroup("Truss", [&] { return std::make_shared<Truss>(Vector3(), Vector3(1,1,1), Cube::defaultTextureID); });
            makeGroup("Seat", [&] { return std::make_shared<Seat>(Vector3(), Vector3(1,1,1), Cube::defaultTextureID); });
            makeGroup("Sphere", [&] { return std::make_shared<Sphere>(Vector3(), Vector3(1,1,1)); });
            makeGroup("MeshCube", [&] { return std::make_shared<MeshCube>(Vector3(), Vector3(1,1,1)); });
            makeGroup("LiquidCube", [&] { return std::make_shared<LiquidCube>(Vector3(), Vector3(4,2,4)); });
            makeGroup("SpawnLocation", [&] { return std::make_shared<SpawnLocation>(Vector3()); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryEffects))) {
            if (AudioService::instance) makeGroup("Sound", [&] { return std::make_shared<Sound>(*AudioService::instance); });
            makeGroup("Decal", [&] { return std::make_shared<Decal>(0, Face::Front); });
            makeGroup("Texture", [&] { return std::make_shared<Texture>(0, Face::Front); });
            makeGroup("SurfaceMark", [&] { return std::make_shared<SurfaceMark>(Vector3(), Vector3(4, 4, 4)); });
            makeGroup("PostEffect", [&] { return std::make_shared<PostEffect>(); });
            makeGroup("ParticleEmitter", [&] { return std::make_shared<ParticleEmitter>(); });
            makeGroup("Highlight", [&] { return std::make_shared<Highlight>(); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryEnvironment))) {
            makeGroup("Workspace", [&] { return std::make_shared<Workspace>(); });
            makeGroup("Weather", [&] { return std::make_shared<Weather>(); });
            if (ImGui::MenuItem("Terrain") && !targets.empty()) {
                m_pendingGroupTargets = targets;
                m_pendingTerrainParent = targets.front()->Parent.lock();
                m_pendingTerrainName = "Terrain";
                m_openTerrainDialog = true;
            }
            makeGroup("Skybox", [&] { return std::make_shared<Skybox>(); });
            makeGroup("Lighting", [&] { return std::make_shared<Lighting>(); });
            makeGroup("PointLight", [&] { return std::make_shared<PointLight>(); });
            makeGroup("SpotLight", [&] { return std::make_shared<SpotLight>(); });
            makeGroup("Sun", [&] {
                auto sun = std::make_shared<Sun>();
                if (m_user) {
                    float rad = sun->Angle * (3.14159265f / 180.0f);
                    sun->cframe.Position = m_user->cpos + Vector3(0.0f, std::sin(rad), std::cos(rad)) * 1000.0f;
                }
                return sun;
            });
            makeGroup("Moon", [&] {
                auto moon = std::make_shared<Moon>();
                float angle = 45.0f;
                auto parent = targets.empty() ? std::shared_ptr<Instance>() : targets.front()->Parent.lock();
                if (parent) for (auto const& [name, child] : parent->children)
                    if (child && child->IsA("Sun")) { angle = static_cast<Sun*>(child.get())->Angle; break; }
                if (m_user) {
                    float rad = angle * (3.14159265f / 180.0f);
                    moon->cframe.Position = m_user->cpos - Vector3(0.0f, std::sin(rad), std::cos(rad)) * 1000.0f;
                }
                return moon;
            });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryGui))) {
            makeGroup("TextLabel", [&] { return std::make_shared<TextLabel>(); });
            makeGroup("TextButton", [&] { return std::make_shared<TextButton>(); });
            makeGroup("SurfaceGui", [&] { return std::make_shared<SurfaceGui>(); });
            makeGroup("Canvas", [&] { return std::make_shared<Canvas>(); });
            makeGroup("BillboardGui", [&] { return std::make_shared<BillboardGui>(); });
            makeGroup("ProximityPrompt", [&] { return std::make_shared<ProximityPrompt>(); });
            makeGroup("ImageLabel", [&] { return std::make_shared<ImageLabel>(); });
            makeGroup("ImageButton", [&] { return std::make_shared<ImageButton>(); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryPhysicsConstraints))) {
            makeGroup("Weld", [&] { return std::make_shared<Weld>(); });
            makeGroup("Motor", [&] { return std::make_shared<Motor>(); });
            makeGroup("Rod", [&] { return std::make_shared<Rod>(); });
            makeGroup("BallSocket", [&] { return std::make_shared<BallSocket>(); });
            makeGroup("NoCollision", [&] { return std::make_shared<NoCollision>(); });
            makeGroup("Rope", [&] { return std::make_shared<Rope>(); });
            makeGroup("Attachment", [&] { return std::make_shared<Attachment>(); });
            makeGroup("Force", [&] { return std::make_shared<Force>(); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::CategoryValue))) {
            makeGroup("IntValue", [&] { return std::make_shared<IntValue>(); });
            makeGroup("BoolValue", [&] { return std::make_shared<BoolValue>(); });
            makeGroup("NumberValue", [&] { return std::make_shared<NumberValue>(); });
            makeGroup("Vector3Value", [&] { return std::make_shared<Vector3Value>(); });
            makeGroup("Color4Value", [&] { return std::make_shared<Color4Value>(); });
            makeGroup("CFrameValue", [&] { return std::make_shared<CFrameValue>(); });
            makeGroup("QuaternionValue", [&] { return std::make_shared<QuaternionValue>(); });
            makeGroup("ObjectValue", [&] { return std::make_shared<ObjectValue>(); });
            ImGui::EndMenu();
        }
            if (ImGui::MenuItem("Script") && !targets.empty()) {
                m_pendingGroupTargets = targets;
                m_pendingScriptParent = targets.front()->Parent.lock();
                m_pendingScriptClass = ScriptInsertClass::Script;
                m_scriptDialogError.clear();
                m_openScriptDialog = true;
            }
            if (ImGui::MenuItem("LocalScript") && !targets.empty()) {
                m_pendingGroupTargets = targets;
                m_pendingScriptParent = targets.front()->Parent.lock();
                m_pendingScriptClass = ScriptInsertClass::LocalScript;
                m_scriptDialogError.clear();
                m_openScriptDialog = true;
            }
            if (ImGui::MenuItem("ModuleScript") && !targets.empty()) {
                m_pendingGroupTargets = targets;
                m_pendingScriptParent = targets.front()->Parent.lock();
                m_pendingScriptClass = ScriptInsertClass::ModuleScript;
                m_scriptDialogError.clear();
                m_openScriptDialog = true;
            }
            for (const auto& type : PhysicalFileInstanceRegistry::types()) {
                if (type.insertCategory != PhysicalFileInsertCategory::Other) continue;
                if (std::string(type.className) == "TextFile") continue;
                const std::string className(type.className);
                makeGroup(className.c_str(), [&] {
                    return PhysicalFileInstanceRegistry::create(type.className);
                });
            }
            makeGroup("AppImage", [&] { return std::make_shared<AppImage>(); });
            makeGroup("StarterCharacter", [&] { return std::make_shared<StarterCharacter>(); });
            makeGroup("Humanoid", [&] { return std::make_shared<Humanoid>(); });
            makeGroup("Animation", [&] { return std::make_shared<Animation>(); });
            makeGroup("SignalEvent", [&] { return std::make_shared<SignalEvent>(); });
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

#endif

    ImGui::Separator();

    // --- Delete ---（右クリック対象が複数選択に含まれていれば選択中すべてを削除）
    if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuDelete), "BackSpace") && m_history) {
        bool inSelection = std::find(selectedInstances.begin(), selectedInstances.end(), inst)
                           != selectedInstances.end();
        std::vector<Instance*> targets =
            (inSelection && selectedInstances.size() > 1) ? selectedInstances
                                                          : std::vector<Instance*>{ inst };
        // 祖先が削除集合に含まれる子孫は除外（二重削除防止）
        auto ancestorInTargets = [&targets](Instance* x) {
            for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                if (std::find(targets.begin(), targets.end(), p.get()) != targets.end()) return true;
            return false;
        };
        auto group = std::make_unique<CompositeCommand>();
        for (Instance* target : targets) {
            if (!target || ancestorInTargets(target)) continue;
            auto parent = target->Parent.lock();
            if (!parent) continue;
            auto it = parent->children.find(target->Name);
            if (it == parent->children.end()) continue;
            group->add(std::make_unique<RemoveInstanceCommand>(parent, target->Name, it->second));
        }
        if (!group->empty()) {
            m_history->execute(std::move(group));
            selectedInstances.clear();
            selectedInstance = nullptr;
        }
    }

    ImGui::Separator();

    // --- Copy（選択中すべて。祖先が選択済みの子孫は除外） ---
    if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuCopy), "Ctrl+C") && m_clipboard) {
        m_clipboard->clear();
        if (!selectedInstances.empty()) {
            std::vector<std::shared_ptr<Instance>> roots;
            auto ancestorSel = [&](Instance* x) {
                for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                    if (std::find(selectedInstances.begin(), selectedInstances.end(), p.get())
                        != selectedInstances.end()) return true;
                return false;
            };
            for (Instance* s : selectedInstances)
                if (s && !ancestorSel(s)) roots.push_back(s->shared_from_this());
            *m_clipboard = Instance::cloneForest(roots);
        } else {
            m_clipboard->push_back(inst->cloneTree());
        }
    }

    // clipboard 全要素を parent 配下へ追加する複合コマンドを実行（1 Undo）
    auto pasteAll = [&](std::shared_ptr<Instance> parent) {
        if (!parent || !m_history) return;
        auto group = std::make_unique<CompositeCommand>();
        std::unordered_set<std::string> taken;
        auto clones = Instance::cloneForest(*m_clipboard);
        for (auto& cloned : clones) {
            std::string name = SceneHierarchyPanel::uniqueName(parent, cloned->Name, &taken);
            cloned->Name = name;
            taken.insert(name);
            group->add(std::make_unique<AddInstanceCommand>(parent, cloned));
        }
        if (!group->empty()) m_history->execute(std::move(group));
    };

    bool canPaste = m_clipboard && !m_clipboard->empty();
    // --- Paste (sibling) ---
    if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuPaste), "Ctrl+V", false, canPaste) && m_history) {
        if (auto parent = inst->Parent.lock()) pasteAll(parent);
    }

    // --- Paste as Child ---
    if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuPasteAsChild), "Ctrl+Shift+V", false, canPaste) && m_history) {
        pasteAll(inst->shared_from_this());
    }

    renderClassPicker();
    if (m_classPickerCloseParent) {
        ImGui::CloseCurrentPopup();
        m_classPickerCloseParent = false;
    }
}
