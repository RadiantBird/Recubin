#include <Editor/EditorManager.hpp>

#include <Editor/GuiAutomation.hpp>
#include <Editor/UiHelpers.hpp>
#include <Editor/WelcomePanel.hpp>
#include <Core/Renderer.hpp>
#include <Core/Packager.hpp>
#include <Editor/SpawnUtil.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/Localization.hpp>
#include <Instances/Cube.hpp>
#include <Instances/System.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Truss.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Skybox.hpp>
#include <Editor/IconsDef.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Force.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Animation.hpp>
#include <Instances/Model.hpp>
#include <Instances/PathfindingService.hpp>
#include <Core/CharacterRig.hpp>
#include <Core/SceneLoader.hpp>
#include <Instances/System.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>
#include <string>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <unordered_set>
#include <Util/AssetPath.hpp>
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

namespace {

struct EditorThemeColors {
    ImVec4 toolbarActive   = ImVec4(0.28f, 0.48f, 0.82f, 1.0f);
    ImVec4 toolbarInactive = ImVec4(0.14f, 0.25f, 0.48f, 1.0f);
    ImVec4 toolbarHover    = ImVec4(0.22f, 0.39f, 0.70f, 1.0f);
};

const EditorThemeColors& editorThemeColors() {
    static const EditorThemeColors colors;
    return colors;
}

struct R6AnimationBindingTarget {
    std::shared_ptr<Instance> character;
    std::shared_ptr<Humanoid> humanoid;
};

R6AnimationBindingTarget findR6AnimationBindingTarget(Instance* system) {
    if (!system) return {};
    Instance* starterRaw = system->getChild("StarterCharacter");
    if (!starterRaw || !starterRaw->IsA("StarterCharacter")) return {};
    static constexpr const char* R6_PARTS[] = {
        "Root", "Torso", "Head", "LeftArm", "RightArm", "LeftLeg", "RightLeg"
    };
    for (const char* part : R6_PARTS) {
        if (!starterRaw->getChild(part)) return {};
    }
    Instance* humanoidRaw = starterRaw->getChild("Humanoid");
    if (!humanoidRaw || !humanoidRaw->IsA("Humanoid")) return {};
    return {
        starterRaw->shared_from_this(),
        std::static_pointer_cast<Humanoid>(humanoidRaw->shared_from_this())
    };
}

std::shared_ptr<Animation> addDefaultR6Walk(const R6AnimationBindingTarget& target,
                                             const std::string& contentPath) {
    if (!target.character || !target.humanoid) return nullptr;
    auto animation = std::make_shared<Animation>();
    animation->Name = "R6Walk";
    YAML::Node pathNode;
    pathNode = contentPath;
    animation->setProperty("ContentPath", pathNode);
    target.character->addChild(animation);
    target.humanoid->setWalkAnimation(animation);
    return animation;
}

std::string migrateLegacyWalkContentPath(const std::string& currentScenePath,
                                         const std::string& legacyStoredPath) {
    namespace fs = std::filesystem;
    if (legacyStoredPath.empty()) return {};

    std::error_code ec;
    fs::path source = AssetPath::fromStored(legacyStoredPath);
    if (source.is_relative())
        source = fs::path(currentScenePath).parent_path() / source;
    const fs::path absolute = fs::absolute(source, ec).lexically_normal();
    if (ec) return AssetPath::toStored(source.lexically_normal());

    // New Animation.ContentPath is project/CWD-relative. Preserve portability
    // only when the legacy file resolves and stays within the current project.
    if (fs::exists(absolute, ec) && !ec) {
        const fs::path projectPath = fs::current_path(ec);
        if (!ec) {
            const fs::path relative = fs::relative(absolute, projectPath, ec);
            if (!ec && !relative.empty() && *relative.begin() != "..")
                return AssetPath::toStored(relative.lexically_normal());
        }
    }
    return AssetPath::toStored(absolute);
}

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
    welcomePanel        = std::make_unique<WelcomePanel>();
    welcomePanel->isOpen = false; // 既定は非表示。起動時に main.cpp が true にする

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
    contentBrowserPanel->selectedInstance = &hierarchyPanel->selectedInstance;
    contentBrowserPanel->workspace        = &hierarchyPanel->workspace;

    // 履歴に変更が入ったら未保存(dirty)にする。これで全パネル（挿入/貼付/削除/
    // D&D/リネーム/プロパティ編集）が CommandHistory 経由で自動的に dirty 化される
    m_history.setOnChange([this]{ m_isDirty = true; });

    // CommandHistory と clipboard を各パネルに渡す
    hierarchyPanel->m_history   = &m_history;
    hierarchyPanel->m_clipboard = &m_clipboard;
    propertiesPanel->m_history  = &m_history;
    viewportPanel->m_history    = &m_history;
    contentBrowserPanel->m_history = &m_history;

    propertiesPanel->m_picker = &m_picker;
    viewportPanel->m_picker   = &m_picker;
    hierarchyPanel->m_picker  = &m_picker;

    propertiesPanel->m_terrainBrush = &m_terrainBrush;
    viewportPanel->m_terrainBrush   = &m_terrainBrush;

    propertiesPanel->m_decalPlace = &m_decalPlace;
    viewportPanel->m_decalPlace   = &m_decalPlace;
    viewportPanel->m_weldMode     = &m_weldMode;

    // メインビューポートをデフォルトでフォーカス状態に設定
    ViewportFocusManager::getInstance().onFocusViewport(viewportPanel.get());

    // ようこそタブのボタン → 既存のシーン操作へ委譲
    welcomePanel->onNewScene  = [this]{ requestNewScene(); };
    welcomePanel->onLoadLast  = [this]{ requestSceneLoad(welcomePanel->lastScenePath); };
    welcomePanel->onOpenScene = [this]{ openSceneDialog(); return !pendingLoadPath.empty(); };

    applyTheme();
}

EditorPlayMode EditorManager::selectedPlayMode() const {
    return m_selectedPlayMode;
}

void EditorManager::setSelectedPlayMode(EditorPlayMode playMode) {
    switch (playMode) {
        case EditorPlayMode::Normal:
        case EditorPlayMode::PlayHere:
        case EditorPlayMode::LocalServer:
            m_selectedPlayMode = playMode;
            break;
    }
}

EditorPlayMode EditorManager::activePlayMode() const {
    return m_activePlayMode;
}

int EditorManager::networkClientCount() const {
    return m_networkClientCount;
}

void EditorManager::setNetworkClientCount(int count) {
    m_networkClientCount = std::clamp(count, 1, 8);
}

void EditorManager::setNetworkClientStatus(int connected, int expected) {
    m_connectedClientCount = (std::max)(connected, 0);
    m_expectedClientCount = (std::max)(expected, 0);
}

void EditorManager::setExternalPlayCleanup(bool cleaningUp) {
    m_externalPlayCleanup = cleaningUp;
}

bool EditorManager::isExternalPlayCleanup() const {
    return m_externalPlayCleanup;
}

void EditorManager::showPlayStartError(const std::string& detail) {
    m_playStartErrorKind = PlayStartErrorKind::Generic;
    m_playStartErrorDetail = detail;
    m_showPlayStartError = true;
}

void EditorManager::showLocalServerNetworkRequiredError() {
    m_playStartErrorKind = PlayStartErrorKind::NetworkRequired;
    m_playStartErrorDetail.clear();
    m_showPlayStartError = true;
}

void EditorManager::render(GLFWwindow* window) {
    updateResponsiveScale();

    // Edit中と専用サーバー観察中は L キーによるモード切替をブロック
    if (m_user) {
        const bool localServerActive = !isEditMode() && m_activePlayMode == EditorPlayMode::LocalServer;
        m_user->allowControlModeSwitch = !isEditMode() && !localServerActive;
    }
    cleanupOrphanedSelection();

    // ---- エディターショートカット処理 ----
    if (isEditMode()) handleEditorShortcuts();

    // ---- 未保存ダイアログ ----
    renderSaveDialog();
    // ---- テストプレイ中のシーン読み込み確認ダイアログ ----
    renderPlayLoadConfirmDialog();
    // ---- テストプレイ開始エラー ----
    renderPlayStartErrorDialog();
    renderSceneLoadErrorDialog();
    renderRestoreR6Dialog();

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
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuNewScene), "Ctrl+N") && isEditMode()) requestNewScene();
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuSaveScene), "Ctrl+S") && isEditMode()) saveCurrentScene();
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuOpenScene), "Ctrl+O")) openSceneDialog();
            ImGui::BeginDisabled(!isEditMode());
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::MenuRestoreDefaultR6Animations)))
                m_showRestoreR6Confirm = true;
            ImGui::EndDisabled();
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
            ImGui::MenuItem(Loc::t(Loc::LocKey::PanelWelcome),        nullptr, &welcomePanel->isOpen);
            ImGui::Separator();
            ImGui::MenuItem(Loc::t(Loc::LocKey::MenuPhysicsDebug),    nullptr, &viewportPanel->showPhysicsDebug);
            ImGui::MenuItem(Loc::t(Loc::LocKey::MenuRenderingDebug), nullptr, &viewportPanel->showRenderingDebug);
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
    welcomePanel->dockspaceId = dockId;
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
    welcomePanel->title        = std::string(Loc::t(Loc::LocKey::PanelWelcome)) + "###Welcome";

    if (hierarchyPanel->isOpen)      hierarchyPanel->onRender();
    if (propertiesPanel->isOpen)     propertiesPanel->onRender();
    if (viewportPanel->isOpen)       viewportPanel->onRender();
    if (contentBrowserPanel->isOpen) contentBrowserPanel->onRender();
    if (consolePanel->isOpen)        consolePanel->onRender();
    if (animationPanel->isOpen)      animationPanel->onRender();
    if (welcomePanel->isOpen)        welcomePanel->onRender();

    // ---- セカンダリビューポート ----
    // 閉じられたものを削除
    secondaryViewports.erase(
        std::remove_if(secondaryViewports.begin(), secondaryViewports.end(),
                       [](const std::unique_ptr<ViewportPanel>& sv) {
                           if (!sv->isOpen) {
                               ViewportFocusManager::getInstance().onViewportDestroyed(sv.get());
                           }
                           return !sv->isOpen;
                       }),
        secondaryViewports.end());
    for (auto& sv : secondaryViewports) {
        sv->onRender();
    }

    renderPackageDialog();
    renderNavMeshBuildOverlay();
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
    panel->m_weldMode   = &m_weldMode;
    panel->title = std::string(Loc::t(Loc::LocKey::PanelViewport)) + ": " + ws->Name + "###SecVP_" + std::to_string(reinterpret_cast<std::uintptr_t>(panel.get()));
    panel->m_useOwnCamera = true;
    panel->initOwnCameraFrom(*m_user);
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

    // Ctrl+N: シーンの新規作成
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) {
        requestNewScene();
        return;
    }

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
        // Basic toolbar: number-row shortcuts select tools without the toggle behavior
        // of toolbar buttons.  ImGuiKey_1..4 are the top-row keys (not keypad keys).
        if (m_toolbarCategory == ToolbarCategory::Basic) {
            ViewportPanel* targetViewport = GetFocusedViewport();
            if (!targetViewport) targetViewport = GetLastFocusedViewport();
            if (!targetViewport) targetViewport = viewportPanel.get();
            if (targetViewport) {
                ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                bool pressed = false;
                int toolKey = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_1)) toolKey = 1;
                else if (ImGui::IsKeyPressed(ImGuiKey_2)) toolKey = 2;
                else if (ImGui::IsKeyPressed(ImGuiKey_3)) toolKey = 3;
                else if (ImGui::IsKeyPressed(ImGuiKey_4)) toolKey = 4;
                if (toolKey == 1) {
                    targetViewport->toolNone = false;
                    targetViewport->selectOnly = true;
                    pressed = true;
                } else if (toolKey == 2) {
                    operation = ImGuizmo::TRANSLATE; pressed = true;
                } else if (toolKey == 3) {
                    operation = ImGuizmo::SCALE; pressed = true;
                } else if (toolKey == 4) {
                    operation = ImGuizmo::ROTATE; pressed = true;
                }
                if (pressed && toolKey != 1) {
                    targetViewport->toolNone = false;
                    targetViewport->selectOnly = false;
                    targetViewport->gizmoOp = operation;
                }
            }
        }
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
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) {
            ViewportPanel* targetViewport = GetFocusedViewport();
            if (!targetViewport) targetViewport = GetLastFocusedViewport();
            if (!targetViewport) targetViewport = viewportPanel.get();
            if (targetViewport) {
                targetViewport->gizmoMode = (targetViewport->gizmoMode == ImGuizmo::WORLD)
                    ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            }
        }

        // Ctrl+F: 選択中インスタンスをエクスプローラーで自動展開・スクロール
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
            if (hierarchyPanel->selectedInstance)
                hierarchyPanel->requestReveal(hierarchyPanel->selectedInstance);
        }

        // F2: 選択中インスタンスをアウトライナー上でインラインリネーム開始
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && !GetFocusedViewport() && hierarchyPanel->selectedInstance &&
            !hierarchyPanel->selectedInstance->isRuntimeNameLocked()) {
            hierarchyPanel->renamingInstance   = hierarchyPanel->selectedInstance;
            hierarchyPanel->renameFocusPending = true;
        }

        // BackSpace: 選択インスタンスをすべて削除（複数選択対応・1 Undo 単位）
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            auto& si = hierarchyPanel->selectedInstances;
            std::vector<Instance*> primarySelection;
            const std::vector<Instance*>* targets = &si;
            if (si.empty() && hierarchyPanel->selectedInstance) {
                primarySelection.push_back(hierarchyPanel->selectedInstance);
                targets = &primarySelection;
            }
            // 祖先が選択集合に含まれる子孫は除外（親を消すと子も消えるため二重削除を防ぐ）
            auto ancestorSelected = [targets](Instance* x) {
                for (auto p = x->Parent.lock(); p; p = p->Parent.lock())
                    if (std::find(targets->begin(), targets->end(), p.get()) != targets->end()) return true;
                return false;
            };
            auto group = std::make_unique<CompositeCommand>();
            for (Instance* target : *targets) {
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
                std::vector<std::shared_ptr<Instance>> roots;
                m_clipboard.clear();
                for (Instance* s : sel)
                    if (s && !ancestorInSet(s, sel)) roots.push_back(s->shared_from_this());
                m_clipboard = Instance::cloneForest(roots);
            }
        }

        // 複数ペースト: clipboard 全要素を parent 配下へ追加する複合コマンドを実行（1 Undo）
        auto pasteInto = [&](std::shared_ptr<Instance> parent) {
            if (!parent) return;
            auto group = std::make_unique<CompositeCommand>();
            std::unordered_set<std::string> taken;
            std::vector<Instance*> pasted;
            auto clones = Instance::cloneForest(m_clipboard);
            for (auto& cloned : clones) {
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
    // 無題シーン: 保存先を決める（キャンセルなら何もしない = dirty維持）
    if (scenePath.empty()) {
        std::string path = getPlatform().saveFileDialog({{"Scene (*.yaml;*.yml)", "*.yaml;*.yml"}}, "yaml");
        if (path.empty()) return;
        scenePath = path;
    }
    // System とその全ての子（Workspace, Lighting など）を保存
    Instance* saveRoot = m_system ? m_system : static_cast<Instance*>(m_workspace);
    if (SceneLoader::saveSceneResult(saveRoot, scenePath, m_sceneMetadata)) m_isDirty = false;
}

void EditorManager::showSceneLoadError(const std::string& message) {
    m_loadError = message;
    m_showLoadError = true;
}

void EditorManager::evaluateSceneMigration() {
    if (!m_workspace) return;
    const auto result = migrateCharacterAnimationBindings(
        m_system, scenePath, m_sceneMetadata);
    if (result != CharacterAnimationMigrationResult::Inserted &&
        result != CharacterAnimationMigrationResult::RecordedOnly) return;
    m_isDirty = true;
    RCBN_LOG((result == CharacterAnimationMigrationResult::Inserted
        ? "Character animation bindings migrated: added an explicit R6 Walk Animation reference."
        : "Character animation bindings migration recorded; existing WalkAnimation was preserved."));
}

EditorManager::CharacterAnimationMigrationResult
EditorManager::migrateCharacterAnimationBindings(
    Instance* system, const std::string& scenePath,
    SceneLoader::SceneDocumentMetadata& metadata) {
    if (scenePath.empty()) return CharacterAnimationMigrationResult::NotApplicable;
    if (metadata.characterAnimationBindingsVersion >= 1)
        return CharacterAnimationMigrationResult::AlreadyMigrated;
    const auto target = findR6AnimationBindingTarget(system);
    if (!target.character || !target.humanoid)
        return CharacterAnimationMigrationResult::NotApplicable;

    bool insertedWalk = false;
    if (target.humanoid->getWalkAnimationPath().empty()) {
        const std::string contentPath = metadata.legacyWalkContentPath.empty()
            ? "assets/anims/r6_walk.rcanim"
            : migrateLegacyWalkContentPath(scenePath, metadata.legacyWalkContentPath);
        insertedWalk = addDefaultR6Walk(target, contentPath) != nullptr;
    }
    metadata.characterAnimationBindingsVersion = 1;
    return insertedWalk ? CharacterAnimationMigrationResult::Inserted
                        : CharacterAnimationMigrationResult::RecordedOnly;
}

bool EditorManager::restoreDefaultR6Bindings(Instance* system) {
    const auto target = findR6AnimationBindingTarget(system);
    return addDefaultR6Walk(target, "assets/anims/r6_walk.rcanim") != nullptr;
}

void EditorManager::restoreDefaultR6Animations() {
    if (!restoreDefaultR6Bindings(m_system)) {
        showSceneLoadError(Loc::t(Loc::LocKey::RestoreDefaultR6Unavailable));
        return;
    }
    m_sceneMetadata.characterAnimationBindingsVersion = 1;
    m_isDirty = true;
}

void EditorManager::renderSceneLoadErrorDialog() {
    if (!m_showLoadError) return;
    const char* title = Loc::t(Loc::LocKey::SceneLoadErrorTitle);
    ImGui::OpenPopup(title); m_showLoadError = false;
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_loadError.c_str());
    if (ImGui::Button(Loc::t(Loc::LocKey::OK))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorManager::renderRestoreR6Dialog() {
    const char* title = Loc::t(Loc::LocKey::RestoreDefaultR6Title);
    if (m_showRestoreR6Confirm) {
        ImGui::OpenPopup(title);
        m_showRestoreR6Confirm = false;
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("%s", Loc::t(Loc::LocKey::RestoreDefaultR6Message));
    if (ImGui::Button(Loc::t(Loc::LocKey::RestoreDefaultR6Button))) {
        restoreDefaultR6Animations();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Loc::t(Loc::LocKey::Cancel))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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

void EditorManager::requestNewScene() {
    if (!isEditMode()) return; // Play中は無視（Save/Packageと同じゲート方針）
    pendingNewScene = true;
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

void EditorManager::renderPlayStartErrorDialog() {
    if (m_showPlayStartError) {
        ImGui::OpenPopup("###PlayStartError");
        m_showPlayStartError = false;
    }

    std::string popupTitle = std::string(Loc::t(Loc::LocKey::PlayStartErrorTitle)) + "###PlayStartError";
    // TextWrapped needs a usable content width; auto-sizing the popup to its
    // unwrapped text can collapse it to a narrow, very tall window.
    ImGui::SetNextWindowSize(ImVec2(420.0f * m_uiLayoutScale, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, 0)) {
        ImGui::TextWrapped("%s", Loc::t(Loc::LocKey::PlayStartErrorMessage));
        if (m_playStartErrorKind == PlayStartErrorKind::NetworkRequired) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", Loc::t(Loc::LocKey::LocalServerRequiresNetwork));
        }
        if (!m_playStartErrorDetail.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_playStartErrorDetail.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button(Loc::t(Loc::LocKey::OK), ImVec2(90.0f * m_uiLayoutScale, 0))) {
            m_playStartErrorDetail.clear();
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

        const float quitCooldown = GuiAutomation::enabled() ? 0.0f : 3.0f;
        if (EditorUi::dangerButton(Loc::t(Loc::LocKey::SaveAndQuit), m_saveDialogOpenedAt, quitCooldown)) {
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
        const bool quitWithoutSavingReady = ImGui::GetTime() - m_saveDialogOpenedAt >= quitCooldown;
        if (EditorUi::dangerButton(Loc::t(Loc::LocKey::QuitWithoutSaving), m_saveDialogOpenedAt, quitCooldown)) {
            m_isDirty = false;
            hierarchyPanel->selectedInstance = nullptr;
            hierarchyPanel->selectedInstances.clear();
            m_history.clear();
            m_clipboard.clear();
            if (m_dialogWindow) glfwSetWindowShouldClose(m_dialogWindow, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        if (quitWithoutSavingReady)
            GuiAutomation::registerLastItem("Editor/UnsavedChanges/QuitWithoutSaving");
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
            if (m_system && m_system->IsA("System"))
                cfg.applicationId = static_cast<System*>(m_system)->ApplicationId;

            auto logFn = [this](const std::string& msg) { m_pkgLog.push_back(msg); m_pkgLogScrollToBottom = true; };
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
        std::string pkgCopyLabel = std::string(Loc::t(Loc::LocKey::MenuCopy)) + "##pkgcopy";
        if (ImGui::SmallButton(pkgCopyLabel.c_str())) {
            std::string joined;
            for (const auto& line : m_pkgLog) {
                joined += line;
                joined += '\n';
            }
            ImGui::SetClipboardText(joined.c_str());
        }
        ImGui::BeginChild("##pkglog", ImVec2(0, 0), true);
        for (const auto& line : m_pkgLog) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (m_pkgLogScrollToBottom) { ImGui::SetScrollHereY(1.0f); m_pkgLogScrollToBottom = false; }
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
    const float toolbarHeight = 140.0f * m_uiLayoutScale;
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, toolbarHeight), ImGuiCond_Always);

    ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(8.0f * m_uiLayoutScale, 10.0f * m_uiLayoutScale));
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
    const auto& colors = editorThemeColors();
    const ImVec2 tabBtnSz = ImVec2(110.0f * m_uiLayoutScale,
                                    30.0f * m_uiLayoutScale);

    auto tabButton = [&](Loc::LocKey key, ToolbarCategory cat) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            (m_toolbarCategory == cat) ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(nullptr, Loc::t(key), tabBtnSz)) m_toolbarCategory = cat;
        ImGui::PopStyleColor(2);
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
    if (!activeViewport) activeViewport = GetLastFocusedViewport();
    if (!activeViewport) activeViewport = viewportPanel.get();

    const auto& colors = editorThemeColors();
    const float scale = m_uiLayoutScale;
    const ImVec2 iconBtnSz = ImVec2(78.0f * scale, 58.0f * scale);
    const ImVec2 playBtnSz = ImVec2(120.0f * scale, 58.0f * scale);

    auto toolbarSeparator = [scale] {
        ImGui::SameLine();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float height = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(pos.x + 4.0f * scale, pos.y + 3.0f * scale),
            ImVec2(pos.x + 4.0f * scale, pos.y + height - 3.0f * scale),
            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2(9.0f * scale, height));
        ImGui::SameLine();
    };

    // ---- Play方式 / Client数 / Play / Pause / Stop ----
    ImGui::BeginGroup();
    const bool localServerActive = !isEditMode() && m_activePlayMode == EditorPlayMode::LocalServer;
    const bool lockPlaySelectors = localServerActive || m_externalPlayCleanup;
    const char* playModePreview = Loc::t(Loc::LocKey::PlayModeNormal);
    switch (m_selectedPlayMode) {
        case EditorPlayMode::Normal:      playModePreview = Loc::t(Loc::LocKey::PlayModeNormal); break;
        case EditorPlayMode::PlayHere:    playModePreview = Loc::t(Loc::LocKey::PlayModeHere); break;
        case EditorPlayMode::LocalServer: playModePreview = Loc::t(Loc::LocKey::PlayModeLocalServer); break;
    }

    ImGui::SetNextItemWidth(playBtnSz.x);
    ImGui::BeginDisabled(lockPlaySelectors);
    if (ImGui::BeginCombo("##EditorPlayMode", playModePreview)) {
        auto playModeItem = [this](EditorPlayMode candidate, Loc::LocKey labelKey) {
            const bool selected = m_selectedPlayMode == candidate;
            if (ImGui::Selectable(Loc::t(labelKey), selected)) m_selectedPlayMode = candidate;
            if (selected) ImGui::SetItemDefaultFocus();
        };
        playModeItem(EditorPlayMode::Normal, Loc::LocKey::PlayModeNormal);
        playModeItem(EditorPlayMode::PlayHere, Loc::LocKey::PlayModeHere);
        playModeItem(EditorPlayMode::LocalServer, Loc::LocKey::PlayModeLocalServer);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Loc::t(Loc::LocKey::PlayModeLabel));

    if (m_selectedPlayMode == EditorPlayMode::LocalServer) {
        ImGui::SameLine();
        char clientPreview[64] = {};
        std::snprintf(clientPreview, sizeof(clientPreview),
                      Loc::t(Loc::LocKey::NetworkClientCountFormat), m_networkClientCount);
        ImGui::SetNextItemWidth(112.0f * scale);
        if (ImGui::BeginCombo("##NetworkClientCount", clientPreview)) {
            for (int count = 1; count <= 8; ++count) {
                char itemLabel[64] = {};
                std::snprintf(itemLabel, sizeof(itemLabel),
                              Loc::t(Loc::LocKey::NetworkClientCountFormat), count);
                const bool selected = m_networkClientCount == count;
                if (ImGui::Selectable(itemLabel, selected)) m_networkClientCount = count;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", Loc::t(Loc::LocKey::NetworkClientCountLabel));
        }
    }
    ImGui::EndDisabled();

    if (mode == EditorMode::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.18f, 1.0f));
        ImGui::BeginDisabled(m_externalPlayCleanup);
        if (drawIconButton(ICON_PLAY, Loc::t(Loc::LocKey::PlayButton), playBtnSz)) {
            m_activePlayMode = m_selectedPlayMode;
            if (m_activePlayMode == EditorPlayMode::LocalServer) {
                m_connectedClientCount = 0;
                m_expectedClientCount = m_networkClientCount;
            }
            mode = EditorMode::Play;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
            mode == EditorMode::Pause ? ImVec4(0.7f, 0.55f, 0.0f, 1.0f)
                                      : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        ImGui::BeginDisabled(localServerActive);
        if (drawIconButton(ICON_PAUSE, Loc::t(Loc::LocKey::PauseButton), iconBtnSz))
            mode = (mode == EditorMode::Pause) ? EditorMode::Play : EditorMode::Pause;
        ImGui::EndDisabled();
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

        if (localServerActive) {
            ImGui::SameLine();
            ImGui::Text(Loc::t(Loc::LocKey::NetworkClientStatus),
                        m_connectedClientCount, m_expectedClientCount);
        }
    }
    ImGui::EndGroup();

    toolbarSeparator();

    // ---- Select / Move / Resize / Rotate ----
    if (activeViewport) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            activeViewport->isSelectMode() ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(ICON_SELECT, Loc::t(Loc::LocKey::SelectTool), iconBtnSz)) {
            if (activeViewport->isSelectMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = true;
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            activeViewport->isMoveMode() ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(ICON_MOVE, Loc::t(Loc::LocKey::MoveTool), iconBtnSz)) {
            if (activeViewport->isMoveMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = false;
                activeViewport->gizmoOp    = ImGuizmo::TRANSLATE;
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            activeViewport->isResizeMode() ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(ICON_RESIZE, Loc::t(Loc::LocKey::ResizeTool), iconBtnSz)) {
            if (activeViewport->isResizeMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = false;
                activeViewport->gizmoOp    = ImGuizmo::SCALE;
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            activeViewport->isRotateMode() ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(ICON_ROTATE, Loc::t(Loc::LocKey::RotateTool), iconBtnSz)) {
            if (activeViewport->isRotateMode()) {
                activeViewport->toolNone = true;
            } else {
                activeViewport->toolNone   = false;
                activeViewport->selectOnly = false;
                activeViewport->gizmoOp    = ImGuizmo::ROTATE;
            }
        }
        ImGui::PopStyleColor(2);

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

    toolbarSeparator();

    // ---- スナップ / 衝突フィット ----
    if (activeViewport) {
        std::string snapTLabel = std::string(Loc::t(Loc::LocKey::SnapTranslate)) + "##snapT";
        ImGui::Checkbox(snapTLabel.c_str(), &activeViewport->snapTranslate);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f * scale);
        if (!activeViewport->snapTranslate) ImGui::BeginDisabled();
        ImGui::DragFloat("studs##snapTVal", &activeViewport->snapTranslateVal,
                         0.005f, 0.001f, 100.0f, "%.3f");
        if (!activeViewport->snapTranslate) ImGui::EndDisabled();
        ImGui::SameLine();

        std::string snapRLabel = std::string(Loc::t(Loc::LocKey::SnapRotate)) + "##snapR";
        ImGui::Checkbox(snapRLabel.c_str(), &activeViewport->snapRotate);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f * scale);
        if (!activeViewport->snapRotate) ImGui::BeginDisabled();
        ImGui::DragFloat("\xc2\xb0##snapRVal", &activeViewport->snapRotateVal,
                         0.05f, 0.001f, 180.0f, "%.3f");
        if (!activeViewport->snapRotate) ImGui::EndDisabled();
        ImGui::SameLine();

        std::string snapSLabel = std::string(Loc::t(Loc::LocKey::SnapScale)) + "##snapS";
        ImGui::Checkbox(snapSLabel.c_str(), &activeViewport->snapScale);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f * scale);
        if (!activeViewport->snapScale) ImGui::BeginDisabled();
        ImGui::DragFloat("studs##snapSVal", &activeViewport->snapScaleVal,
                         0.005f, 0.001f, 100.0f, "%.3f");
        if (!activeViewport->snapScale) ImGui::EndDisabled();
        ImGui::SameLine();

        std::string cfLabel = std::string(Loc::t(Loc::LocKey::CollisionFit)) + "##cf";
        ImGui::Checkbox(cfLabel.c_str(), &activeViewport->collisionFit);
        ImGui::SameLine();
    }

    toolbarSeparator();

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
    ImGui::SameLine(ImGui::GetWindowWidth() - saveLoadW - 8.0f * scale);

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
    const ImVec2 btnSz(78.0f * m_uiLayoutScale, 58.0f * m_uiLayoutScale);

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
    ImGui::SameLine();
    tryAddObjectButton<SpawnLocation>(ICON_CUBE, "SpawnLocation", "SpawnLocation", ws, btnSz,
        spawnPos);
}

void EditorManager::renderToolbarTerrain() {
    const ImVec2 btnSz(78.0f * m_uiLayoutScale, 58.0f * m_uiLayoutScale);
    const auto& colors = editorThemeColors();

    ImGui::PushStyleColor(ImGuiCol_Button,
        m_terrainBrush.active ? colors.toolbarActive : colors.toolbarInactive);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
    if (drawIconButton(ICON_TERRAINBRUSH_TOGGLE, Loc::t(Loc::LocKey::TerrainBrushEdit), btnSz))
        m_terrainBrush.active = !m_terrainBrush.active;
    ImGui::PopStyleColor(2);

    if (!m_terrainBrush.active) return;

    ImGui::SameLine();

    // Sculpt/Paint 切り替え
    {
        ImGui::PushStyleColor(ImGuiCol_Button,
            !m_terrainBrush.paintMode ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(ICON_TERRAINBRUSH_TOGGLE, Loc::t(Loc::LocKey::TerrainBrushSculptTab), btnSz)) m_terrainBrush.paintMode = false;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            m_terrainBrush.paintMode ? colors.toolbarActive : colors.toolbarInactive);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
        if (drawIconButton(ICON_TERRAIN_PAINT, Loc::t(Loc::LocKey::TerrainBrushPaintTab), btnSz)) m_terrainBrush.paintMode = true;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }

    if (!m_terrainBrush.paintMode) {
        auto modeButton = [&](const char* icon, Loc::LocKey key, int modeValue) {
            bool active = (m_terrainBrush.mode == modeValue);
            ImGui::PushStyleColor(ImGuiCol_Button,
                active ? colors.toolbarActive : colors.toolbarInactive);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.toolbarHover);
            if (drawIconButton(icon, Loc::t(key), btnSz)) m_terrainBrush.mode = modeValue;
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
        };
        modeButton(ICON_TERRAIN_LOWER,  Loc::LocKey::TerrainBrushModeLower,  -1);
        modeButton(ICON_TERRAIN_SMOOTH, Loc::LocKey::TerrainBrushModeSmooth,  0);
        modeButton(ICON_TERRAIN_RAISE,  Loc::LocKey::TerrainBrushModeRaise,  +1);
    } else {
        ImGui::ColorEdit3(Loc::t(Loc::LocKey::TerrainBrushPaintColor), m_terrainBrush.paintColor);
        ImGui::SameLine();
    }

    ImGui::SetNextItemWidth(160.0f * m_uiLayoutScale);
    ImGui::SliderFloat(Loc::t(Loc::LocKey::TerrainBrushRadius), &m_terrainBrush.radius, 1.0f, 64.0f, "%.1f studs");
    ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::TerrainBrushHint));
}

void EditorManager::renderToolbarPhysics() {
    Instance* sel = getSelectedInstance();
    std::shared_ptr<Instance> parent = sel ? sel->shared_from_this()
                                            : (m_workspace ? m_workspace->shared_from_this() : nullptr);
    const ImVec2 btnSz(78.0f * m_uiLayoutScale, 58.0f * m_uiLayoutScale);

    ImGui::PushStyleColor(ImGuiCol_Button, m_weldMode.active
        ? ImVec4(0.15f, 0.68f, 0.28f, 1.0f)
        : ImVec4(0.22f, 0.40f, 0.70f, 0.60f));
    if (drawIconButton(ICON_WELD, "Weld Mode", btnSz)) {
        m_weldMode.active = !m_weldMode.active;
        m_weldMode.cube0.reset();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

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
    ImGui::SameLine();
    tryAddObjectButton<BallSocket>(ICON_BALLSOCKET, "BallSocket", "BallSocket", parent, btnSz);
    ImGui::SameLine();
    tryAddObjectButton<NoCollision>(ICON_NOCOLLISION, "NoCollision", "NoCollision", parent, btnSz);
}

void EditorManager::renderToolbarCharacter() {
    const ImVec2 btnSz(78.0f * m_uiLayoutScale, 58.0f * m_uiLayoutScale);
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
        CharacterRig::buildDefaultRigParts(model);
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

bool EditorManager::ownsSceneRender() {
    return true;
}

void EditorManager::clearForImGui(GLFWwindow* window) {
    int winW, winH;
    glfwGetFramebufferSize(window, &winW, &winH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void EditorManager::renderUI(User& user, GLFWwindow* window, Workspace& workspace) {
    if (Renderer::instance) Renderer::instance->prepareGuiFonts(workspace);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    GuiAutomation::beforeNewFrame();
    ImGui::NewFrame();
    GuiAutomation::afterNewFrame();
    ImGuizmo::BeginFrame();

    render(window);

    if (Renderer::instance) Renderer::instance->drawCameraRotationCursor(user, window);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    GuiAutomation::afterRender(window);

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
}

void IEditorManager::renderNavMeshBuildOverlay() {
    const bool isActive = PathfindingService::IsBuildActive();
    const std::string title =
        std::string(Loc::t(Loc::LocKey::NavMeshBuildTitle)) + "###NavMeshBuildProgress";

    if (isActive) {
        ImGui::OpenPopup(title.c_str());
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport) {
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);

    if (!ImGui::BeginPopupModal(
            title.c_str(), nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return;
    }

    if (!isActive) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const float progress = std::clamp(PathfindingService::GetBuildProgress(), 0.0f, 1.0f);
    ImGui::TextUnformatted(Loc::t(Loc::LocKey::NavMeshBuildMessage));
    ImGui::Spacing();
    ImGui::Text("%s: %d%%",
                Loc::t(Loc::LocKey::NavMeshBuildProgress),
                static_cast<int>(progress * 100.0f + 0.5f));
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "");
    ImGui::EndPopup();
}

// ===================================================
//  カスタムテーマ（ダークエディター調）
// ===================================================
void EditorManager::updateResponsiveScale() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float dpiScale = (viewport && viewport->DpiScale > 0.0f)
        ? viewport->DpiScale
        : 1.0f;

    // 1920px 幅で従来の密度になることを基準にする。高DPIかつ表示領域が
    // 狭い場合はフォントと各種余白を同じ比率で縮め、タブやツールバーが
    // DPI倍率だけで必要以上に広がらないようにする。
    constexpr float REFERENCE_WIDTH = 1920.0f;
    constexpr float MIN_FIT_SCALE = 0.55f;
    const float workWidth = viewport
        ? (std::max)(viewport->WorkSize.x, 1.0f)
        : REFERENCE_WIDTH;
    const float fitScale = (std::max)(
        (std::min)(1.0f, workWidth / (REFERENCE_WIDTH * dpiScale)),
        MIN_FIT_SCALE);

    m_uiLayoutScale = dpiScale * fitScale;

    ImGuiStyle& style = ImGui::GetStyle();

    // ConfigDpiScaleFonts による FontScaleDpi と組み合わせ、実効フォント倍率を
    // m_uiLayoutScale と一致させる。これによりドッキングタブ、タイトルバー、
    // メニュー、ツリー、入力欄、コンソールにも同じ補正が適用される。
    style.FontScaleMain = fitScale;

    // ScaleAllSizes() は呼ぶたびに値が累積するため使わず、基準値から毎フレーム
    // 再計算する。モニター間移動やウィンドウサイズ変更にも即座に追従できる。
    style.WindowPadding     = ImVec2(8.0f * m_uiLayoutScale, 8.0f * m_uiLayoutScale);
    style.WindowMinSize     = ImVec2(32.0f * m_uiLayoutScale, 32.0f * m_uiLayoutScale);
    style.FramePadding      = ImVec2(6.0f * m_uiLayoutScale, 4.0f * m_uiLayoutScale);
    style.ItemSpacing       = ImVec2(8.0f * m_uiLayoutScale, 5.0f * m_uiLayoutScale);
    style.ItemInnerSpacing  = ImVec2(4.0f * m_uiLayoutScale, 4.0f * m_uiLayoutScale);
    style.CellPadding       = ImVec2(4.0f * m_uiLayoutScale, 2.0f * m_uiLayoutScale);
    style.IndentSpacing     = 21.0f * m_uiLayoutScale;
    style.ColumnsMinSpacing = 6.0f * m_uiLayoutScale;
    style.ScrollbarSize     = 14.0f * m_uiLayoutScale;
    style.GrabMinSize       = 12.0f * m_uiLayoutScale;

    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f * m_uiLayoutScale;
    style.PopupRounding     = 2.0f * m_uiLayoutScale;
    style.ScrollbarRounding = 3.0f * m_uiLayoutScale;
    style.GrabRounding      = 2.0f * m_uiLayoutScale;
    style.TabRounding       = 1.0f * m_uiLayoutScale;

    // 細線はDPI倍率で太くせず、既存の視覚的な軽さを維持する。
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;
    style.FrameBorderSize  = 0.0f;
}

void EditorManager::applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 1.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8, 5);
    style.FramePadding      = ImVec2(6, 4);

    ImVec4* c = style.Colors;

    c[ImGuiCol_WindowBg]          = ImVec4(0.075f, 0.105f, 0.25f, 1.0f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.065f, 0.090f, 0.21f, 1.0f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.075f, 0.095f, 0.22f, 0.98f);
    c[ImGuiCol_Border]            = ImVec4(0.18f, 0.22f, 0.32f, 1.0f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);

    c[ImGuiCol_Header]            = ImVec4(0.13f, 0.24f, 0.43f, 0.70f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.20f, 0.35f, 0.62f, 0.78f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.23f, 0.42f, 0.78f, 1.0f);

    c[ImGuiCol_Button]            = ImVec4(0.14f, 0.25f, 0.48f, 1.0f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.22f, 0.39f, 0.70f, 1.0f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.28f, 0.48f, 0.86f, 1.0f);

    c[ImGuiCol_FrameBg]           = ImVec4(0.095f, 0.115f, 0.16f, 1.0f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.14f, 0.18f, 0.25f, 1.0f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.20f, 0.28f, 0.42f, 1.0f);

    c[ImGuiCol_Tab]               = ImVec4(0.055f, 0.085f, 0.22f, 1.0f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.16f, 0.29f, 0.58f, 1.0f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.22f, 0.43f, 0.82f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.50f, 0.75f, 1.0f, 1.0f);

    c[ImGuiCol_TitleBg]           = ImVec4(0.045f, 0.070f, 0.18f, 1.0f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.19f, 0.42f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.045f, 0.070f, 0.18f, 0.8f);

    c[ImGuiCol_Text]              = ImVec4(0.88f, 0.90f, 0.94f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.38f, 0.42f, 0.50f, 1.0f);

    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.30f, 0.35f, 0.45f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.44f, 0.56f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.44f, 0.52f, 0.68f, 1.0f);

    c[ImGuiCol_Separator]         = ImVec4(0.18f, 0.22f, 0.32f, 0.75f);
    c[ImGuiCol_SeparatorHovered]  = ImVec4(0.30f, 0.50f, 0.82f, 0.78f);
    c[ImGuiCol_SeparatorActive]   = ImVec4(0.30f, 0.50f, 0.82f, 1.0f);

    c[ImGuiCol_DockingPreview]    = ImVec4(0.30f, 0.55f, 0.95f, 0.48f);
    c[ImGuiCol_DockingEmptyBg]    = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);

    ImGuizmo::GetStyle().CenterCircleSize = 0.0f;
}
