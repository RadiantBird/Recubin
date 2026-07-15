#include <Editor/EditorManager.hpp>

#include <Editor/UiHelpers.hpp>
#include <Core/Renderer.hpp>
#include <Core/Packager.hpp>
#include <Editor/SpawnUtil.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/Localization.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Truss.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Skybox.hpp>
#include <Editor/IconsDef.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Force.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Model.hpp>
#include <Core/CharacterRig.hpp>
#include <Core/SceneLoader.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>
#include <string>
#include <cstdio>
#include <algorithm>
#include <unordered_set>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>

static bool treeContainsInstance(Instance* root, Instance* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    for (auto& [name, child] : root->children) {
        if (treeContainsInstance(child.get(), target)) return true;
    }
    return false;
}

// ===================================================
//  EditorManager 実装
// ===================================================

EditorManager::EditorManager(Workspace* workspace, User* user, Instance* system) {
    m_workspace = workspace;
    m_system    = system;
    m_user      = user;

    consolePanel        = std::make_unique<ConsolePanel>();
    hierarchyPanel      = std::make_unique<SceneHierarchyPanel>();
    propertiesPanel     = std::make_unique<PropertiesPanel>();
    contentBrowserPanel = std::make_unique<ContentBrowserPanel>();
    viewportPanel       = std::make_unique<ViewportPanel>();
    animationPanel      = std::make_unique<AnimationEditorPanel>();
    animationPanel->isOpen = false; // 既定では非表示

    hierarchyPanel->workspace   = workspace;
    hierarchyPanel->systemRoot  = system;
    hierarchyPanel->m_user      = user;
    viewportPanel->user         = user;
    viewportPanel->workspace    = workspace;

    // selectedInstance ポインタを共有（SceneHierarchy が書き、Properties/Viewport が読む）
    propertiesPanel->selectedInstance  = &hierarchyPanel->selectedInstance;
    propertiesPanel->selectedInstances = &hierarchyPanel->selectedInstances;
    viewportPanel->selectedInstance    = &hierarchyPanel->selectedInstance;
    viewportPanel->selectedInstances   = &hierarchyPanel->selectedInstances;
    animationPanel->selectedInstance   = &hierarchyPanel->selectedInstance;
    animationPanel->m_history          = &m_history;

    // 履歴に変更が入ったら未保存(dirty)にする。これで全パネル（挿入/貼付/削除/
    // D&D/リネーム/プロパティ編集）が CommandHistory 経由で自動的に dirty 化される
    m_history.setOnChange([this]{ m_isDirty = true; });

    // CommandHistory と clipboard を各パネルに渡す
    hierarchyPanel->m_history   = &m_history;
    hierarchyPanel->m_clipboard = &m_clipboard;
    propertiesPanel->m_history  = &m_history;
    viewportPanel->m_history    = &m_history;

    propertiesPanel->m_picker = &m_picker;
    viewportPanel->m_picker   = &m_picker;
    hierarchyPanel->m_picker  = &m_picker;

    propertiesPanel->m_terrainBrush = &m_terrainBrush;
    viewportPanel->m_terrainBrush   = &m_terrainBrush;

    propertiesPanel->m_decalPlace = &m_decalPlace;
    viewportPanel->m_decalPlace   = &m_decalPlace;

    // メインビューポートをデフォルトでフォーカス状態に設定
    ViewportFocusManager::getInstance().onFocusViewport(viewportPanel.get());

    applyTheme();
}

void EditorManager::render(GLFWwindow* window) {
    // Edit モード中は L キーによるモード切替をブロック
    if (m_user) m_user->allowControlModeSwitch = !isEditMode();
    cleanupOrphanedSelection();

    // ---- エディターショートカット処理 ----
    if (isEditMode()) handleEditorShortcuts();

    // ---- 未保存ダイアログ ----
    renderSaveDialog();
    // ---- テストプレイ中のシーン読み込み確認ダイアログ ----
    renderPlayLoadConfirmDialog();

    // ---- 全画面 DockSpace ----
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize      | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    // ---- メニューバー ----
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::MenuFile))) {
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuSaveScene), "Ctrl+S") && isEditMode()) saveCurrentScene();
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuOpenScene), "Ctrl+O")) openSceneDialog();
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuPackageGame)) && isEditMode()) {
                m_pkgLog.clear();
                m_showPackageDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuQuit), "Alt+F4")) {
                if (m_isDirty) requestSaveDialog(window);
                else if (window) glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::MenuView))) {
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelExplorer), nullptr, &hierarchyPanel->isOpen); // Scene Hierarchy を Explorer に改名
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelProperties),     nullptr, &propertiesPanel->isOpen);
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelViewport),       nullptr, &viewportPanel->isOpen);
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelContentBrowser), nullptr, &contentBrowserPanel->isOpen);
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelConsole),        nullptr, &consolePanel->isOpen);
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelAnimation),      nullptr, &animationPanel->isOpen);
            ImGui::Separator();
            ImGui::MenuItem(Loc::t(Loc::LocKey::MenuPhysicsDebug),    nullptr, &viewportPanel->showPhysicsDebug);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::t(Loc::LocKey::MenuSettings))) {
            bool isJa = Loc::getLanguage() == Loc::Lang::JA;
            bool isEn = !isJa;
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::LanguageJapanese), nullptr, isJa))
                Loc::setLanguage(Loc::Lang::JA);
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::LanguageEnglish), nullptr, isEn))
                Loc::setLanguage(Loc::Lang::EN);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // DockSpace 本体
    ImGuiID dockId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    ImGui::End(); // DockSpaceHost

    // ---- ツールバー（Play / Pause / Stop）----
    renderToolbar();

    // ---- 各パネル ----
    // パネルタイトルは毎フレーム現在の言語で更新する（###以降のIDは固定のまま、表示のみ切替）。
    // viewportPanel はセカンダリビューポートと同じ ViewportPanel クラスを共有しているため、
    // タイトルの動的組み立て（ワークスペース名付き等）と衝突しないようここ（メイン側のみ）で設定する。
    hierarchyPanel->title      = std::string(Loc::t(Loc::LocKey::PanelExplorer)) + "###Explorer";
    propertiesPanel->title     = std::string(Loc::t(Loc::LocKey::PanelProperties)) + "###Properties";
    viewportPanel->title       = std::string(Loc::t(Loc::LocKey::PanelViewport)) + "###Viewport";
    contentBrowserPanel->title = std::string(Loc::t(Loc::LocKey::PanelContentBrowser)) + "###Content Browser";
    consolePanel->title        = std::string(Loc::t(Loc::LocKey::PanelConsole)) + "###Console";
    animationPanel->title      = std::string(Loc::t(Loc::LocKey::AnimationEditorWindowTitle)) + "###Animation Editor";

    if (hierarchyPanel->isOpen)      hierarchyPanel->onRender();
    if (propertiesPanel->isOpen)     propertiesPanel->onRender();
    if (viewportPanel->isOpen)       viewportPanel->onRender();
    if (contentBrowserPanel->isOpen) contentBrowserPanel->onRender();
    if (consolePanel->isOpen)        consolePanel->onRender();
    if (animationPanel->isOpen)      animationPanel->onRender();

    // ---- セカンダリビューポート ----
    for (auto& sv : secondaryViewports) {
        sv->onRender();
    }
    // 閉じられたものを削除
    secondaryViewports.erase(
        std::remove_if(secondaryViewports.begin(), secondaryViewports.end(),
                       [](const std::unique_ptr<ViewportPanel>& sv) {
                           if (!sv->isOpen && IsViewportFocused(sv.get())) {
                               ClearViewportFocus();
                           }
                           return !sv->isOpen;
                       }),
        secondaryViewports.end());

    renderPackageDialog();
}

void EditorManager::openSecondaryViewport(Workspace* ws) {
    if (!ws) return;
    auto wsSp = std::static_pointer_cast<Workspace>(ws->shared_from_this());
    auto panel = std::make_unique<ViewportPanel>();
    panel->workspace = ws;
    panel->user = m_user;
    panel->selectedInstance = &hierarchyPanel->selectedInstance;
    panel->selectedInstances = &hierarchyPanel->selectedInstances;
    panel->m_history = &m_history;
    panel->m_picker = &m_picker;
    panel->m_terrainBrush = &m_terrainBrush;
    panel->m_decalPlace = &m_decalPlace;
    panel->title = std::string(Loc::t(Loc::LocKey::PanelViewport)) + ": " + ws->Name + "###SecVP_" + std::to_string(reinterpret_cast<std::uintptr_t>(panel.get()));
    secondaryViewports.push_back(std::move(panel));
}

bool EditorManager::isAnyViewportHovered() const {
    if (viewportPanel && viewportPanel->isHoveringViewport) {
        return true;
    }
    for (auto const& sv : secondaryViewports) {
        if (sv && sv->isHoveringViewport) {
            return true;
        }
    }
    return false;
}

void EditorManager::handleEditorShortcuts() {
    // テキスト入力中はショートカットをスキップ
    bool textActive = ImGui::GetIO().WantTextInput;

    // Ctrl+S: 保存
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        saveCurrentScene();
        return;
    }

    // Ctrl+O: シーンを開く
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
        openSceneDialog();
        return;
    }

    if (!textActive) {
        // Ctrl+Z: Undo
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            m_history.undo();
            cleanupOrphanedSelection();
        }
        // Ctrl+Shift+Z: Redo
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
            m_history.redo();
            cleanupOrphanedSelection();
        }

        // Ctrl+L: ギズモのワールド/ローカル軸をトグル
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L) && viewportPanel) {
            viewportPanel->gizmoMode = (viewportPanel->gizmoMode == ImGuizmo::WORLD)
                ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        }

        // Ctrl+F: 選択中インスタンスをエクスプローラーで自動展開・スクロール
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
            if (hierarchyPanel->selectedInstance)
                hierarchyPanel->requestReveal(hierarchyPanel->selectedInstance);
        }

        // F2: 選択中インスタンスをアウトライナー上でインラインリネーム開始
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && !GetFocusedViewport() && hierarchyPanel->selectedInstance) {
            hierarchyPanel->renamingInstance   = hierarchyPanel->selectedInstance;
            hierarchyPanel->renameFocusPending = true;
        }

        // BackSpace: 選択インスタンスをすべて削除（複数選択対応・1 Undo 単位）
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !GetFocusedViewport()) {
            auto& si = hierarchyPanel->selectedInstances;
            // 祖先が選択集合に含まれる子孫は除外（親を消すと子も消えるため二重削除を防ぐ）
            auto ancestorSelected = [&si](Instance* x) {
                for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                    if (std::find(si.begin(), si.end(), p.get()) != si.end()) return true;
                return false;
            };
            auto group = std::make_unique<CompositeCommand>();
            for (Instance* target : si) {
                if (!target || ancestorSelected(target)) continue;
                auto parent = target->Parent.lock();
                if (!parent) continue;
                auto it = parent->children.find(target->Name);
                if (it == parent->children.end()) continue;
                group->add(std::make_unique<RemoveInstanceCommand>(parent, target->Name, it->second));
            }
            if (!group->empty()) {
                m_history.execute(std::move(group));
                si.clear();
                hierarchyPanel->selectedInstance = nullptr;
                m_isDirty = true;
            }
        }

        // 祖先がセットに含まれるか（複数コピーで子孫の二重処理を防ぐ）
        auto ancestorInSet = [](Instance* x, const std::vector<Instance*>& set) {
            for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                if (std::find(set.begin(), set.end(), p.get()) != set.end()) return true;
            return false;
        };

        // Ctrl+C: コピー（選択中すべて。祖先が選択済みの子孫は除外）
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            auto& sel = hierarchyPanel->selectedInstances;
            if (!sel.empty()) {
                m_clipboard.clear();
                for (Instance* s : sel)
                    if (s && !ancestorInSet(s, sel)) m_clipboard.push_back(s->cloneTree());
            }
        }

        // 複数ペースト: clipboard 全要素を parent 配下へ追加する複合コマンドを実行（1 Undo）
        auto pasteInto = [&](std::shared_ptr<Instance> parent) {
            if (!parent) return;
            auto group = std::make_unique<CompositeCommand>();
            std::unordered_set<std::string> taken;
            std::vector<Instance*> pasted;
            for (auto& item : m_clipboard) {
                auto cloned = item->cloneTree();
                std::string name = SceneHierarchyPanel::uniqueName(parent, cloned->Name, &taken);
                cloned->Name = name;
                taken.insert(name);
                pasted.push_back(cloned.get());
                group->add(std::make_unique<AddInstanceCommand>(parent, cloned));
            }
            if (!group->empty()) {
                m_history.execute(std::move(group));
                hierarchyPanel->selectedInstances = pasted;
                hierarchyPanel->selectedInstance  = pasted.empty() ? nullptr : pasted.back();
                m_isDirty = true;
            }
        };

        // Ctrl+V: ペースト（primary の兄弟として / なければ Workspace 直下）
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) && !m_clipboard.empty()) {
            Instance* sel = hierarchyPanel->selectedInstance;
            std::shared_ptr<Instance> parent;
            if (sel) parent = sel->Parent.lock();
            else if (m_workspace) parent = m_workspace->shared_from_this();
            pasteInto(parent);
        }

        // Ctrl+Shift+V: 選択インスタンスの子としてペースト
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V) && !m_clipboard.empty()) {
            Instance* sel = hierarchyPanel->selectedInstance;
            if (sel) pasteInto(sel->shared_from_this());
        }
    }
}

void EditorManager::saveCurrentScene() {
    if (!m_system && !m_workspace) return;
    // System とその全ての子（Workspace, Lighting など）を保存
    Instance* saveRoot = m_system ? m_system : static_cast<Instance*>(m_workspace);
    SceneLoader::saveScene(saveRoot, scenePath);
    m_isDirty = false;
}

void EditorManager::openSceneDialog() {
    requestSceneLoad(getPlatform().openFileDialog({{"Scene (*.yaml;*.yml)", "*.yaml;*.yml"}}));
}

void EditorManager::requestSceneLoad(const std::string& path) {
    if (path.empty()) return;

    if (isEditMode()) {
        pendingLoadPath = path;
        return;
    }

    // テストプレイ中は即座にロードせず、終了確認ポップアップを挟む
    m_pendingPlayLoadPath = path;
    m_showPlayLoadConfirm = true;
}

void EditorManager::renderPlayLoadConfirmDialog() {
    if (m_showPlayLoadConfirm) {
        ImGui::OpenPopup("###PlayLoadConfirm");
        m_showPlayLoadConfirm = false;
    }

    std::string popupTitle = std::string(Loc::t(Loc::LocKey::PlayLoadTitle)) + "###PlayLoadConfirm";
    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", Loc::t(Loc::LocKey::PlayLoadLine1));
        ImGui::Text("%s", Loc::t(Loc::LocKey::PlayLoadLine2));
        ImGui::Separator();

        if (ImGui::Button(Loc::t(Loc::LocKey::PlayLoadConfirm), ImVec2(150, 0))) {
            mode = EditorMode::Edit;
            if (m_user) {
                m_user->controlMode = User::ControlMode::Free;
                RCBN_LOG("[INFO] Stopped due to scene load request. Switched to Free Camera mode.");
            }
            pendingLoadPath = m_pendingPlayLoadPath;
            m_pendingPlayLoadPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::Cancel), ImVec2(90, 0))) {
            m_pendingPlayLoadPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorManager::requestSaveDialog(GLFWwindow* window) {
    m_showSaveDialog = true;
    m_dialogWindow   = window;
    m_saveDialogOpenedAt = ImGui::GetTime();
}

void EditorManager::renderSaveDialog() {
    if (m_showSaveDialog) {
        ImGui::OpenPopup("###UnsavedChanges");
        m_showSaveDialog = false;
    }

    std::string popupTitle = std::string(Loc::t(Loc::LocKey::UnsavedTitle)) + "###UnsavedChanges";
    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", Loc::t(Loc::LocKey::UnsavedLine1));
        ImGui::Text("%s", Loc::t(Loc::LocKey::UnsavedLine2));
        ImGui::Separator();

        if (EditorUi::dangerButton(Loc::t(Loc::LocKey::SaveAndQuit), m_saveDialogOpenedAt)) {
            saveCurrentScene();
            // GL コンテキストが生きている今のうちに GPU リソースを持つ
            // インスタンスの shared_ptr を解放する（コンテキスト破棄後の
            // glDelete* 呼び出しによるヒープ破壊を防ぐ）
            hierarchyPanel->selectedInstance = nullptr;
            hierarchyPanel->selectedInstances.clear();
            m_history.clear();
            m_clipboard.clear();
            if (m_dialogWindow) glfwSetWindowShouldClose(m_dialogWindow, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorUi::dangerButton(Loc::t(Loc::LocKey::QuitWithoutSaving), m_saveDialogOpenedAt)) {
            m_isDirty = false;
            hierarchyPanel->selectedInstance = nullptr;
            hierarchyPanel->selectedInstances.clear();
            m_history.clear();
            m_clipboard.clear();
            if (m_dialogWindow) glfwSetWindowShouldClose(m_dialogWindow, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorUi::safeButton(Loc::t(Loc::LocKey::Cancel))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorManager::renderPackageDialog() {
    if (m_showPackageDialog) {
        ImGui::OpenPopup("###PackageGame");
        m_showPackageDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
    std::string popupTitle = std::string(Loc::t(Loc::LocKey::PackageGameTitle)) + "###PackageGame";
    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("%s", Loc::t(Loc::LocKey::GameNameLabel));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##pkgname", m_pkgName, sizeof(m_pkgName));

        ImGui::Text("%s", Loc::t(Loc::LocKey::OutputDirLabel));
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##pkgoutdir", m_pkgOutDir, sizeof(m_pkgOutDir));
        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::Browse))) {
            std::string folder = getPlatform().openFolderDialog();
            if (!folder.empty() && folder.size() < sizeof(m_pkgOutDir)) {
                std::snprintf(m_pkgOutDir, sizeof(m_pkgOutDir), "%s", folder.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(m_isPackaging || m_pkgName[0] == '\0' || m_pkgOutDir[0] == '\0');
        if (ImGui::Button(Loc::t(Loc::LocKey::PackageButton), ImVec2(120, 0))) {
            m_pkgLog.clear();
            m_isPackaging = true;

            Packager::Config cfg;
            cfg.gameName      = m_pkgName;
            cfg.outputDir     = m_pkgOutDir;
            cfg.scenePath     = scenePath;
            cfg.engineExePath = engineExePath;

            auto logFn = [this](const std::string& msg) { m_pkgLog.push_back(msg); };
            Packager::package(cfg, logFn);
            m_isPackaging = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(Loc::t(Loc::LocKey::CloseButton), ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }

        if (m_isPackaging) {
            ImGui::SameLine();
            ImGui::Text("%s", Loc::t(Loc::LocKey::ProcessingText));
        }

        ImGui::Separator();
        ImGui::BeginChild("##pkglog", ImVec2(0, 0), true);
        for (const auto& line : m_pkgLog) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (!m_pkgLog.empty()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::EndPopup();
    }
}

bool EditorManager::drawIconButton(const char* icon, const char* label, const ImVec2& btnSize) {
    std::string combined = icon ? (std::string(icon) + "\n" + label) : std::string(label);

    ImGui::SetWindowFontScale(1.0f);
    ImVec2 textSize = ImGui::CalcTextSize(combined.c_str());
    float availW = btnSize.x - ImGui::GetStyle().FramePadding.x * 2.0f;
    float availH = btnSize.y - ImGui::GetStyle().FramePadding.y * 2.0f;

    float scale = 1.0f;
    if (textSize.x > availW && textSize.x > 0.0f) scale = (std::min)(scale, availW / textSize.x);
    if (textSize.y > availH && textSize.y > 0.0f) scale = (std::min)(scale, availH / textSize.y);
    scale = (std::max)(scale, 0.55f); // 可読性下限、これ以上は縮小しない

    ImGui::SetWindowFontScale(scale);
    bool clicked = ImGui::Button(combined.c_str(), btnSize);
    ImGui::SetWindowFontScale(1.0f);
    return clicked;
}

template <typename T, typename... Args>
void EditorManager::tryAddObjectButton(const char* icon, const std::string& label,
                                        const std::string& defaultName,
                                        const std::shared_ptr<Instance>& parent,
                                        const ImVec2& btnSize, Args&&... args)
{
    // parentがnullでも(disabled状態のボタン等)レイアウト位置を安定させるため、描画は必ず行う。
    // 実際にインスタンスを追加するのはparentがある場合のみ。
    if (drawIconButton(icon, label.c_str(), btnSize) && parent) {
        auto obj = std::make_shared<T>(std::forward<Args>(args)...);
        obj->Name = SceneHierarchyPanel::uniqueName(parent, defaultName);
        m_history.execute(std::make_unique<AddInstanceCommand>(parent, obj));
        m_isDirty = true;
    }
}

void EditorManager::renderToolbar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 110.0f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 10.0f));
    ImGui::Begin("Toolbar", nullptr, tbFlags);
    ImGui::PopStyleVar();

    renderToolbarTabs();

    switch (m_toolbarCategory) {
        case ToolbarCategory::Basic:     renderToolbarBasic();     break;
        case ToolbarCategory::Cubes:     renderToolbarCubes();     break;
        case ToolbarCategory::Terrain:   renderToolbarTerrain();   break;
        case ToolbarCategory::Physics:   renderToolbarPhysics();   break;
        case ToolbarCategory::Character: renderToolbarCharacter(); break;
    }

    ImGui::End();
}

void EditorManager::renderToolbarTabs() {
    const ImVec4 colActive   = ImVec4(0.30f, 0.50f, 0.85f, 1.0f);
    const ImVec4 colInactive = ImVec4(0.22f, 0.40f, 0.70f, 0.60f);
    const ImVec2 tabBtnSz    = ImVec2(110.0f, 30.0f);

    auto tabButton = [&](Loc::LocKey key, ToolbarCategory cat) {
        ImGui::PushStyleColor(ImGuiCol_Button, (m_toolbarCategory == cat) ? colActive : colInactive);
        if (drawIconButton(nullptr, Loc::t(key), tabBtnSz)) m_toolbarCategory = cat;
        ImGui::PopStyleColor();
        ImGui::SameLine();
    };

    tabButton(Loc::LocKey::ToolbarTabBasic,     ToolbarCategory::Basic);
    tabButton(Loc::LocKey::ToolbarTabCubes,     ToolbarCategory::Cubes);
    tabButton(Loc::LocKey::ToolbarTabTerrain,   ToolbarCategory::Terrain);
    tabButton(Loc::LocKey::ToolbarTabPhysics,   ToolbarCategory::Physics);
    tabButton(Loc::LocKey::ToolbarTabCharacter, ToolbarCategory::Character);

    ImGui::NewLine();
}

void EditorManager::renderToolbarBasic() {
    ViewportPanel* activeViewport = GetFocusedViewport();
    if (!activeViewport) activeViewport = viewportPanel.get();

    const ImVec4 colActive   = ImVec4(0.30f, 0.50f, 0.85f, 1.0f);
    const ImVec4 colInactive = ImVec4(0.22f, 0.40f, 0.70f, 0.60f);
    const ImVec2 iconBtnSz   = ImVec2(78.0f, 58.0f);

    // ---- Play / Pause / Stop ----
    if (mode == EditorMode::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.18f, 1.0f));
        if (drawIconButton(ICON_PLAY, Loc::t(Loc::LocKey::PlayButton), iconBtnSz)) mode = EditorMode::Play;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
            mode == EditorMode::Pause ? ImVec4(0.7f, 0.55f, 0.0f, 1.0f)
                                      : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        if (drawIconButton(ICON_PAUSE, Loc::t(Loc::LocKey::PauseButton), iconBtnSz))
            mode = (mode == EditorMode::Pause) ? EditorMode::Play : EditorMode::Pause;
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
        if (drawIconButton(ICON_STOP, Loc::t(Loc::LocKey::StopButton), iconBtnSz)) {
            mode = EditorMode::Edit;
            if (m_user) {
                m_user->controlMode = User::ControlMode::Free;
                RCBN_LOG("[INFO] Stopped. Switched to Free Camera mode.");
            }
            else {
                RCBN_LOG("[???] User instance is null.");
            }

            if (viewportPanel) {
                ViewportFocusManager::getInstance().onFocusViewport(viewportPanel.get());
                ImGui::SetWindowFocus(viewportPanel->title.c_str());
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // ---- Select / Move / Resize / Rotate ----
    if (activeViewport) {
        ImGui::PushStyleColor(ImGuiCol_Button, activeViewport->isSelectMode() ? colActive : colInactive);
        if (drawIconButton(ICON_SELECT, Loc::t(Loc::LocKey::SelectTool), iconBtnSz)) {
            if (activeViewport->isSelectMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = true;
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, activeViewport->isMoveMode() ? colActive : colInactive);
        if (drawIconButton(ICON_MOVE, Loc::t(Loc::LocKey::MoveTool), iconBtnSz)) {
            if (activeViewport->isMoveMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = false;
                activeViewport->gizmoOp    = ImGuizmo::TRANSLATE;
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, activeViewport->isResizeMode() ? colActive : colInactive);
        if (drawIconButton(ICON_RESIZE, Loc::t(Loc::LocKey::ResizeTool), iconBtnSz)) {
            if (activeViewport->isResizeMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = false;
                activeViewport->gizmoOp    = ImGuizmo::SCALE;
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, activeViewport->isRotateMode() ? colActive : colInactive);
        if (drawIconButton(ICON_ROTATE, Loc::t(Loc::LocKey::RotateTool), iconBtnSz)) {
            if (activeViewport->isRotateMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = false;
                activeViewport->gizmoOp    = ImGuizmo::ROTATE;
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // ---- ギズモ軸モード (ワールド/ローカル) トグル (Ctrl+L) ----
        bool isLocalMode = (activeViewport->gizmoMode == ImGuizmo::LOCAL);
        if (drawIconButton(nullptr, isLocalMode ? Loc::t(Loc::LocKey::GizmoLocal) : Loc::t(Loc::LocKey::GizmoWorld), iconBtnSz)) {
            activeViewport->gizmoMode = isLocalMode ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", Loc::t(Loc::LocKey::GizmoModeTooltip));
        }
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // ---- スナップ / 衝突フィット ----
    if (activeViewport) {
        std::string snapTLabel = std::string(Loc::t(Loc::LocKey::SnapTranslate)) + "##snapT";
        ImGui::Checkbox(snapTLabel.c_str(), &activeViewport->snapTranslate);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        ImGui::DragFloat("studs##snapTVal", &activeViewport->snapTranslateVal, 0.1f, 0.1f, 100.0f, "%.1f");
        ImGui::SameLine();

        std::string snapRLabel = std::string(Loc::t(Loc::LocKey::SnapRotate)) + "##snapR";
        ImGui::Checkbox(snapRLabel.c_str(), &activeViewport->snapRotate);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        ImGui::DragFloat("\xc2\xb0##snapRVal", &activeViewport->snapRotateVal, 1.0f, 1.0f, 180.0f, "%.0f");
        ImGui::SameLine();

        std::string snapSLabel = std::string(Loc::t(Loc::LocKey::SnapScale)) + "##snapS";
        ImGui::Checkbox(snapSLabel.c_str(), &activeViewport->snapScale);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        ImGui::DragFloat("studs##snapSVal", &activeViewport->snapScaleVal, 0.1f, 0.1f, 100.0f, "%.1f");
        ImGui::SameLine();

        std::string cfLabel = std::string(Loc::t(Loc::LocKey::CollisionFit)) + "##cf";
        ImGui::Checkbox(cfLabel.c_str(), &activeViewport->collisionFit);
        ImGui::SameLine();
    }

    ImGui::Text("|");
    ImGui::SameLine();

    // ---- New Cube / New Script クイックボタン ----
    if (m_workspace) {
        auto ws = m_workspace->shared_from_this();
        Vector3 spawnPos = computeSpawnPos(m_user, m_workspace);
        tryAddObjectButton<Cube>(ICON_CUBE, "New Cube", "Cube", ws, iconBtnSz,
            spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
    }
    ImGui::SameLine();
    if (drawIconButton(ICON_SCRIPT, Loc::t(Loc::LocKey::NewScriptButton), iconBtnSz) && m_workspace && hierarchyPanel) {
        hierarchyPanel->requestNewScript(m_workspace->shared_from_this());
    }

    // ---- Save / Load（右端）----
    float saveLoadW = iconBtnSz.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - saveLoadW - 8.0f);

    if (drawIconButton(ICON_SAVE, Loc::t(Loc::LocKey::SaveButton), iconBtnSz)) {
        saveCurrentScene();
    }
    ImGui::SameLine();
    if (drawIconButton(ICON_LOAD, Loc::t(Loc::LocKey::LoadButton), iconBtnSz)) {
        requestSceneLoad(getPlatform().openFileDialog({{"Scene (*.yaml;*.yml)", "*.yaml;*.yml"}}));
    }
}

void EditorManager::renderToolbarCubes() {
    if (!m_workspace) return;
    auto ws = m_workspace->shared_from_this();
    Vector3 spawnPos = computeSpawnPos(m_user, m_workspace);
    const ImVec2 btnSz(78.0f, 58.0f);

    tryAddObjectButton<Cube>(ICON_CUBE, "Cube", "Cube", ws, btnSz,
        spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
    ImGui::SameLine();
    tryAddObjectButton<Cylinder>(ICON_CYLINDER, "Cylinder", "Cylinder", ws, btnSz,
        spawnPos, Vector3(1, 1, 1));
    ImGui::SameLine();
    tryAddObjectButton<TriangularPrism>(ICON_TRIANGULARPRISM, "TriangularPrism", "TriangularPrism", ws, btnSz,
        spawnPos, Vector3(1, 1, 1));
    ImGui::SameLine();
    tryAddObjectButton<Truss>(ICON_TRUSS, "Truss", "Truss", ws, btnSz,
        spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
    ImGui::SameLine();
    tryAddObjectButton<Seat>(ICON_SEAT, "Seat", "Seat", ws, btnSz,
        spawnPos, Vector3(1, 1, 1), Cube::defaultTextureID);
    ImGui::SameLine();
    tryAddObjectButton<Sphere>(ICON_SPHERE, "Sphere", "Sphere", ws, btnSz,
        spawnPos, Vector3(1, 1, 1));
    ImGui::SameLine();
    tryAddObjectButton<MeshCube>(ICON_MESHCUBE, "MeshCube", "MeshCube", ws, btnSz,
        spawnPos, Vector3(1, 1, 1));
    ImGui::SameLine();
    tryAddObjectButton<LiquidCube>(ICON_LIQUIDCUBE, "LiquidCube", "LiquidCube", ws, btnSz,
        spawnPos, Vector3(4, 2, 4));
}

void EditorManager::renderToolbarTerrain() {
    const ImVec2 btnSz(78.0f, 58.0f);
    const ImVec4 colActive   = ImVec4(0.30f, 0.50f, 0.85f, 1.0f);
    const ImVec4 colInactive = ImVec4(0.22f, 0.40f, 0.70f, 0.60f);

    ImGui::PushStyleColor(ImGuiCol_Button, m_terrainBrush.active ? colActive : colInactive);
    if (drawIconButton(ICON_TERRAINBRUSH_TOGGLE, Loc::t(Loc::LocKey::TerrainBrushEdit), btnSz))
        m_terrainBrush.active = !m_terrainBrush.active;
    ImGui::PopStyleColor();

    if (!m_terrainBrush.active) return;

    ImGui::SameLine();

    // Sculpt/Paint 切り替え
    {
        ImGui::PushStyleColor(ImGuiCol_Button, !m_terrainBrush.paintMode ? colActive : colInactive);
        if (drawIconButton(ICON_TERRAINBRUSH_TOGGLE, Loc::t(Loc::LocKey::TerrainBrushSculptTab), btnSz)) m_terrainBrush.paintMode = false;
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, m_terrainBrush.paintMode ? colActive : colInactive);
        if (drawIconButton(ICON_TERRAIN_PAINT, Loc::t(Loc::LocKey::TerrainBrushPaintTab), btnSz)) m_terrainBrush.paintMode = true;
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    if (!m_terrainBrush.paintMode) {
        auto modeButton = [&](const char* icon, Loc::LocKey key, int modeValue) {
            bool active = (m_terrainBrush.mode == modeValue);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? colActive : colInactive);
            if (drawIconButton(icon, Loc::t(key), btnSz)) m_terrainBrush.mode = modeValue;
            ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        modeButton(ICON_TERRAIN_LOWER,  Loc::LocKey::TerrainBrushModeLower,  -1);
        modeButton(ICON_TERRAIN_SMOOTH, Loc::LocKey::TerrainBrushModeSmooth,  0);
        modeButton(ICON_TERRAIN_RAISE,  Loc::LocKey::TerrainBrushModeRaise,  +1);
    } else {
        ImGui::ColorEdit3(Loc::t(Loc::LocKey::TerrainBrushPaintColor), m_terrainBrush.paintColor);
        ImGui::SameLine();
    }

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat(Loc::t(Loc::LocKey::TerrainBrushRadius), &m_terrainBrush.radius, 1.0f, 64.0f, "%.1f studs");
    ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::TerrainBrushHint));
}

void EditorManager::renderToolbarPhysics() {
    Instance* sel = getSelectedInstance();
    std::shared_ptr<Instance> parent = sel ? sel->shared_from_this()
                                            : (m_workspace ? m_workspace->shared_from_this() : nullptr);
    const ImVec2 btnSz(78.0f, 58.0f);

    tryAddObjectButton<Weld>(ICON_WELD, "Weld", "Weld", parent, btnSz);
    ImGui::SameLine();
    tryAddObjectButton<Motor>(ICON_MOTOR, "Motor", "Motor", parent, btnSz);
    ImGui::SameLine();
    tryAddObjectButton<Rod>(ICON_ROD, "Rod", "Rod", parent, btnSz);
    ImGui::SameLine();
    tryAddObjectButton<Rope>(ICON_ROPE, "Rope", "Rope", parent, btnSz);
    ImGui::SameLine();
    tryAddObjectButton<Attachment>(ICON_ATTACHMENT, "Attachment", "Attachment", parent, btnSz, Vector3(0, 0, 0));
    ImGui::SameLine();
    tryAddObjectButton<Force>(ICON_FORCE, "Force", "Force", parent, btnSz);
}

void EditorManager::renderToolbarCharacter() {
    const ImVec2 btnSz(78.0f, 58.0f);
    Instance* sel = getSelectedInstance();

    ImGui::BeginDisabled(sel == nullptr);
    tryAddObjectButton<Humanoid>(ICON_HUMANOID, Loc::t(Loc::LocKey::AddHumanoidButton), "Humanoid",
        sel ? sel->shared_from_this() : nullptr, btnSz);
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (drawIconButton(ICON_STARTERCHARACTER, Loc::t(Loc::LocKey::RigBuilderButton), btnSz) && m_workspace) {
        Vector3 spawnPos = computeSpawnPos(m_user, m_workspace);
        auto ws = m_workspace->shared_from_this();
        auto model = std::make_shared<Model>(spawnPos, Vector3(1, 1, 1));
        model->Name = SceneHierarchyPanel::uniqueName(ws, "Model");
        CharacterRig::buildDefaultRigParts(model, spawnPos);
        m_history.execute(std::make_unique<AddInstanceCommand>(ws, model));
        m_isDirty = true;
    }
}

void EditorManager::cleanupOrphanedSelection() {
    Instance* root = m_system ? m_system : static_cast<Instance*>(m_workspace);
    auto& si = hierarchyPanel->selectedInstances;
    si.erase(std::remove_if(si.begin(), si.end(),
        [root](Instance* i){ return !treeContainsInstance(root, i); }), si.end());
    if (!treeContainsInstance(root, hierarchyPanel->selectedInstance)) {
        hierarchyPanel->selectedInstance = si.empty() ? nullptr : si.back();
    }
}

void EditorManager::setWorkspace(Workspace* ws) {
    m_workspace                  = ws;
    hierarchyPanel->workspace    = ws;
    viewportPanel->workspace     = ws;
    hierarchyPanel->selectedInstance = nullptr;
    hierarchyPanel->selectedInstances.clear();
    m_history.clear();
    m_isDirty = false;
}

void EditorManager::beginViewportRender() {
    viewportPanel->beginRender();
}

void EditorManager::endViewportRender() {
    viewportPanel->endRenderAndDisplay();
}

void EditorManager::getViewportSize(GLFWwindow*, int& w, int& h) {
    w = viewportPanel->fbWidth;
    h = viewportPanel->fbHeight;
}

unsigned int EditorManager::getViewportFBO() {
    return viewportPanel->framebuffer;
}

bool EditorManager::isViewportFocused() {
    return GetFocusedViewport() != nullptr;
}

Instance* EditorManager::getSelectedInstance() {
    cleanupOrphanedSelection();
    return hierarchyPanel->selectedInstance;
}

void EditorManager::clearForImGui(GLFWwindow* window) {
    int winW, winH;
    glfwGetFramebufferSize(window, &winW, &winH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void EditorManager::renderUI(User& user, GLFWwindow* window, Workspace&) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    render(window);

    if (Renderer::instance) Renderer::instance->drawCameraRotationCursor(user, window);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
}

// ===================================================
//  カスタムテーマ（ダークエディター調）
// ===================================================
void EditorManager::applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 3.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8, 5);
    style.FramePadding      = ImVec2(6, 4);

    ImVec4* c = style.Colors;

    c[ImGuiCol_WindowBg]          = ImVec4(0.11f, 0.17f, 0.40f, 1.0f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.09f, 0.14f, 0.35f, 1.0f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.09f, 0.14f, 0.35f, 0.98f);
    c[ImGuiCol_Border]            = ImVec4(0.25f, 0.27f, 0.35f, 1.0f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);

    c[ImGuiCol_Header]            = ImVec4(0.22f, 0.40f, 0.70f, 0.55f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.30f, 0.50f, 0.85f, 0.70f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.25f, 0.45f, 0.80f, 1.0f);

    c[ImGuiCol_Button]            = ImVec4(0.22f, 0.40f, 0.70f, 0.60f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.30f, 0.50f, 0.85f, 0.80f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.20f, 0.38f, 0.70f, 1.0f);

    c[ImGuiCol_FrameBg]           = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.20f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.24f, 0.28f, 0.38f, 1.0f);

    c[ImGuiCol_Tab]               = ImVec4(0.08f, 0.12f, 0.32f, 1.0f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.18f, 0.32f, 0.68f, 1.0f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.20f, 0.40f, 0.82f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.50f, 0.75f, 1.0f, 1.0f);

    c[ImGuiCol_TitleBg]           = ImVec4(0.05f, 0.10f, 0.28f, 1.0f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.20f, 0.52f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.05f, 0.10f, 0.28f, 0.8f);

    c[ImGuiCol_Text]              = ImVec4(0.90f, 0.92f, 0.95f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.48f, 0.55f, 1.0f);

    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.30f, 0.35f, 0.45f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.44f, 0.56f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.44f, 0.52f, 0.68f, 1.0f);

    c[ImGuiCol_Separator]         = ImVec4(0.25f, 0.27f, 0.35f, 1.0f);
    c[ImGuiCol_SeparatorHovered]  = ImVec4(0.40f, 0.60f, 0.90f, 0.78f);
    c[ImGuiCol_SeparatorActive]   = ImVec4(0.40f, 0.60f, 0.90f, 1.0f);

    c[ImGuiCol_DockingPreview]    = ImVec4(0.30f, 0.55f, 0.95f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]    = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);

    ImGuizmo::GetStyle().CenterCircleSize = 0.0f;
}
