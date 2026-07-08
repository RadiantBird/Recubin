#include <Editor/SceneHierarchyPanel.hpp>
#include "include/Editor/IconsDef.hpp"
#include <Editor/SpawnUtil.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/PropertiesPanel.hpp>  // PickerState の定義
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
#include <Instances/Script.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/PointLight.hpp>
#include <Instances/SpotLight.hpp>
#include <Instances/PostEffect.hpp>
#include <Core/Terrain.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Force.hpp>
#include <Instances/Model.hpp>
#include <Instances/Folder.hpp>
#include <Instances/FileRef.hpp>
#include <Instances/Tool.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
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
#include <Core/AudioService.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <include/imgui/imgui.h>
#include <fstream>

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
        ImGui::TextDisabled("(No workspace)");
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
    if (!m_pendingReparents.empty() && m_history) {
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
    if (selectedInstance &&
        ImGui::BeginPopupContextWindow("##hier_wnd_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        renderContextMenu(selectedInstance);
        ImGui::EndPopup();
    }

    renderNewScriptDialog();

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
    if (cn == "Script")                                                      return ICON_SCRIPT;
    if (cn == "Sound")                                                       return ICON_SOUND;
    if (cn == "Humanoid")                                                    return ICON_HUMANOID;
    if (cn == "User")                                                        return ICON_USER;
    if (cn == "Decal" || cn == "Texture" ||
        cn == "ImageLabel" || cn == "ImageButton")                           return ICON_DECAL;
    if (cn == "FileRef")                                                     return ICON_FILE;
    if (cn == "Sphere")                                                      return ICON_SPHERE;
    if (cn == "Cube"   || cn == "Cylinder" || cn == "LiquidCube" ||
        cn == "TriangularPrism" || cn == "Truss" || cn == "Seat")            return ICON_CUBE;
    if (cn == "MeshCube") return ICON_MESHCUBE;
    if (cn == "TextLabel"  || cn == "TextButton" || cn == "GuiButton" ||
        cn == "ScreenGui"  || cn == "SurfaceGui" || cn == "BillboardGui" ||
        cn == "WorldGuiObject")                                              return ICON_GUI;
    if (cn == "Rope" || cn == "Rod" || cn == "Weld" || cn == "Motor" ||
        cn == "Attachment" || cn == "Force")                                 return ICON_CONSTRAINT;
    if (cn == "System") return ICON_SYSTEM;
    if (cn == "Weather") return ICON_WEATHER;
    if (cn == "StarterCharacter") return ICON_STARTERCHARACTER;
    if (cn == "AppImage") return ICON_APPIMAGE;
    if (cn == "PathfindingService") return ICON_PATHFINDINGSERVICE;
    if (cn == "PostEffect") return ICON_POSTEFFECT;
    return ICON_INSTANCE;
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

    bool renaming = (inst == renamingInstance);
    bool open = renaming
        ? ImGui::TreeNodeEx(inst, flags, "%s", getClassIcon(inst->getClassName()))
        : ImGui::TreeNodeEx(inst, flags, "%s %s", getClassIcon(inst->getClassName()), inst->Name.c_str());

    if (renaming) {
        ImGui::SameLine();
        if (renameFocusPending) {
            strncpy_s(m_renameBuf, inst->Name.c_str(), sizeof(m_renameBuf) - 1);
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
            const bool matches = m_picker->pickAttachment ? inst->IsA("Attachment")
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
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("INSTANCE_PTR", &inst, sizeof(Instance*));
        ImGui::Text("%s", inst->Name.c_str());
        ImGui::EndDragDropSource();
    }

    // ---- ドロップターゲット ----
    if (ImGui::BeginDragDropTarget()) {
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
            getPlatform().revealInFileManager(filePath);

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
        
        ImGui::TextDisabled("Workspace内で描画される基本的なクラス。");
        ImGui::Separator();
        tryInsertInstance<Cube>(m_history, "Cube", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Cylinder>(m_history, "Cylinder", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<TriangularPrism>(m_history, "TriangularPrism", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<Truss>(m_history, "Truss", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Seat>(m_history, "Seat", parentSp, spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
        tryInsertInstance<Sphere>(m_history, "Sphere", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<MeshCube>(m_history, "MeshCube", parentSp, spawnPos, Vector3(1, 1, 1));
        tryInsertInstance<LiquidCube>(m_history, "LiquidCube", parentSp, spawnPos, Vector3(4, 2, 4));

        ImGui::EndMenu();
    }

    // ---- 効果 ----
    if (ImGui::BeginMenu("効果")) {
        ImGui::TextDisabled("世界を彩りましょう。");
        ImGui::Separator();
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
        tryInsertInstance<PostEffect>(m_history, "PostEffect", parentSp);
        tryInsertInstance<ParticleEmitter>(m_history, "ParticleEmitter", parentSp);
        
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("環境")) {
        ImGui::TextDisabled("7日で宇宙を創るように。");
        ImGui::Separator();

        tryInsertInstance<Workspace>(m_history, "Workspace", parentSp);
        tryInsertInstance<Weather>(m_history, "Weather", parentSp);
        tryInsertInstance<Terrain>(m_history, "Terrain", parentSp);
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
    if (ImGui::BeginMenu("その他")) {
        ImGui::TextDisabled("分類するには少ない...でも重要。");
        ImGui::Separator();

        // Scriptはダイアログを開く特殊な挙動なのでそのまま
        if (ImGui::MenuItem("Script")) {
            m_pendingScriptParent = parentSp;
            m_openScriptDialog    = true;
        }
        
        tryInsertInstance<Folder>(m_history, "Folder", parentSp);
        tryInsertInstance<FileRef>(m_history, "FileRef", parentSp);
        tryInsertInstance<Model>(m_history, "Model", parentSp, Vector3(0, 0, 0), Vector3(1, 1, 1));
        tryInsertInstance<Tool>(m_history, "Tool", parentSp, std::string("Tool"));



        tryInsertInstance<AppImage>(m_history, "AppImage", parentSp);
        tryInsertInstance<StarterCharacter>(m_history, "StarterCharacter", parentSp);
        tryInsertInstance<Humanoid>(m_history, "Humanoid", parentSp);
        tryInsertInstance<Animation>(m_history, "Animation", parentSp);

        ImGui::EndMenu();
    }

    // ---- GUI ----
    if (ImGui::BeginMenu("GUI")) {
        ImGui::TextDisabled("画面にテキストや画像を表示します。");
        ImGui::Separator();
        tryInsertInstance<TextLabel>(m_history, "TextLabel", parentSp);
        tryInsertInstance<TextButton>(m_history, "TextButton", parentSp);
        tryInsertInstance<SurfaceGui>(m_history, "SurfaceGui", parentSp);
        tryInsertInstance<BillboardGui>(m_history, "BillboardGui", parentSp);
        tryInsertInstance<ProximityPrompt>(m_history, "ProximityPrompt", parentSp);
        tryInsertInstance<ImageLabel>(m_history, "ImageLabel", parentSp);
        tryInsertInstance<ImageButton>(m_history, "ImageButton", parentSp);

        ImGui::EndMenu();
    }

    // ---- 物理制約 ----
    if (ImGui::BeginMenu("物理制約")) {
        ImGui::TextDisabled("物理挙動に影響を与えます。");
        ImGui::Separator();
        
        tryInsertInstance<Weld>(m_history, "Weld", parentSp);
        tryInsertInstance<Motor>(m_history, "Motor", parentSp);
        tryInsertInstance<Rod>(m_history, "Rod", parentSp);
        tryInsertInstance<Rope>(m_history, "Rope", parentSp);
        tryInsertInstance<Attachment>(m_history, "Attachment", parentSp);
        tryInsertInstance<Force>(m_history, "Force", parentSp);
        
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

    // --- Delete ---（右クリック対象が複数選択に含まれていれば選択中すべてを削除）
    if (ImGui::MenuItem("Delete", "BackSpace") && m_history) {
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
    if (ImGui::MenuItem("Copy", "Ctrl+C") && m_clipboard) {
        m_clipboard->clear();
        if (!selectedInstances.empty()) {
            auto ancestorSel = [&](Instance* x) {
                for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                    if (std::find(selectedInstances.begin(), selectedInstances.end(), p.get())
                        != selectedInstances.end()) return true;
                return false;
            };
            for (Instance* s : selectedInstances)
                if (s && !ancestorSel(s)) m_clipboard->push_back(s->cloneTree());
        } else {
            m_clipboard->push_back(inst->cloneTree());
        }
    }

    // clipboard 全要素を parent 配下へ追加する複合コマンドを実行（1 Undo）
    auto pasteAll = [&](std::shared_ptr<Instance> parent) {
        if (!parent || !m_history) return;
        auto group = std::make_unique<CompositeCommand>();
        std::unordered_set<std::string> taken;
        for (auto& item : *m_clipboard) {
            auto cloned = item->cloneTree();
            std::string name = SceneHierarchyPanel::uniqueName(parent, cloned->Name, &taken);
            cloned->Name = name;
            taken.insert(name);
            group->add(std::make_unique<AddInstanceCommand>(parent, cloned));
        }
        if (!group->empty()) m_history->execute(std::move(group));
    };

    bool canPaste = m_clipboard && !m_clipboard->empty();
    // --- Paste (sibling) ---
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste) && m_history) {
        if (auto parent = inst->Parent.lock()) pasteAll(parent);
    }

    // --- Paste as Child ---
    if (ImGui::MenuItem("Paste as Child", "Ctrl+Shift+V", false, canPaste) && m_history) {
        pasteAll(inst->shared_from_this());
    }
}
