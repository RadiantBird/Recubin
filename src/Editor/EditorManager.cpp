#include <Editor/EditorManager.hpp>

#include <Core/Packager.hpp>
#include <Editor/SpawnUtil.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <shobjidl.h>
#include <Editor/CommandHistory.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Skybox.hpp>
#include <Core/SceneLoader.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>
#include <string>
#include <algorithm>
#include <Util/Logger.hpp>

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

    hierarchyPanel->workspace   = workspace;
    hierarchyPanel->systemRoot  = system;
    hierarchyPanel->m_user      = user;
    viewportPanel->user         = user;
    viewportPanel->workspace    = workspace;

    // selectedInstance ポインタを共有（SceneHierarchy が書き、Properties/Viewport が読む）
    propertiesPanel->selectedInstance  = &hierarchyPanel->selectedInstance;
    viewportPanel->selectedInstance    = &hierarchyPanel->selectedInstance;
    viewportPanel->selectedInstances   = &hierarchyPanel->selectedInstances;

    // CommandHistory と clipboard を各パネルに渡す
    hierarchyPanel->m_history   = &m_history;
    hierarchyPanel->m_clipboard = &m_clipboard;
    propertiesPanel->m_history  = &m_history;
    viewportPanel->m_history    = &m_history;

    propertiesPanel->m_picker = &m_picker;
    viewportPanel->m_picker   = &m_picker;

    applyTheme();
}

void EditorManager::render(GLFWwindow* window) {
    // Edit モード中は L キーによるモード切替をブロック
    if (m_user) m_user->allowControlModeSwitch = !isEditMode();

    // ---- エディターショートカット処理 ----
    if (isEditMode()) handleEditorShortcuts();

    // ---- 未保存ダイアログ ----
    renderSaveDialog();

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
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S") && isEditMode()) saveCurrentScene();
            if (ImGui::MenuItem("Open Scene", "Ctrl+O") && isEditMode()) openSceneDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Package Game...") && isEditMode()) {
                m_pkgLog.clear();
                m_showPackageDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                if (m_isDirty) requestSaveDialog(window);
                else if (window) glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Hierarchy", nullptr, &hierarchyPanel->isOpen);
            ImGui::MenuItem("Properties",      nullptr, &propertiesPanel->isOpen);
            ImGui::MenuItem("Viewport",        nullptr, &viewportPanel->isOpen);
            ImGui::MenuItem("Content Browser", nullptr, &contentBrowserPanel->isOpen);
            ImGui::MenuItem("Console",         nullptr, &consolePanel->isOpen);
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
    if (hierarchyPanel->isOpen)      hierarchyPanel->onRender();
    if (propertiesPanel->isOpen)     propertiesPanel->onRender();
    if (viewportPanel->isOpen)       viewportPanel->onRender();
    if (contentBrowserPanel->isOpen) contentBrowserPanel->onRender();
    if (consolePanel->isOpen)        consolePanel->onRender();

    // ---- セカンダリビューポート ----
    for (auto& sv : secondaryViewports) {
        sv->onRender();
    }
    // 閉じられたものを削除
    secondaryViewports.erase(
        std::remove_if(secondaryViewports.begin(), secondaryViewports.end(),
                       [](const std::unique_ptr<SecondaryViewportPanel>& sv) { return !sv->m_open; }),
        secondaryViewports.end());

    renderPackageDialog();
}

void EditorManager::openSecondaryViewport(Workspace* ws) {
    if (!ws) return;
    auto wsSp = std::static_pointer_cast<Workspace>(ws->shared_from_this());
    std::string title = "Viewport: " + ws->Name;
    secondaryViewports.push_back(std::make_unique<SecondaryViewportPanel>(wsSp, title));
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

        // BackSpace: 選択インスタンス削除
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !IsViewportFocused(viewportPanel.get())) {
            Instance* sel = hierarchyPanel->selectedInstance;
            if (sel) {
                auto parent = sel->Parent.lock();
                if (parent) {
                    auto childPtr = parent->children.at(sel->Name);
                    m_history.execute(std::make_unique<RemoveInstanceCommand>(
                        parent, sel->Name, childPtr));
                    auto& si = hierarchyPanel->selectedInstances;
                    si.erase(std::remove(si.begin(), si.end(), sel), si.end());
                    hierarchyPanel->selectedInstance = si.empty() ? nullptr : si.back();
                    m_isDirty = true;
                }
            }
        }

        // Ctrl+C: コピー
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            Instance* sel = hierarchyPanel->selectedInstance;
            if (sel) m_clipboard = sel->clone();
        }

        // Ctrl+V: ペースト（兄弟として）
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) && m_clipboard) {
            std::shared_ptr<Instance> parent;
            Instance* sel = hierarchyPanel->selectedInstance;
            if (sel)
                parent = sel->Parent.lock();
            else if (m_workspace)
                parent = m_workspace->shared_from_this();

            if (parent) {
                auto cloned = m_clipboard->clone();
                // 名前衝突を回避
                std::string baseName = cloned->Name;
                int n = 1;
                while (parent->children.count(cloned->Name) > 0)
                    cloned->Name = baseName + std::to_string(n++);
                m_history.execute(std::make_unique<AddInstanceCommand>(parent, cloned));
                m_isDirty = true;
            }
        }

        // Ctrl+Shift+V: 選択インスタンスの子としてペースト
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V) && m_clipboard) {
            Instance* sel = hierarchyPanel->selectedInstance;
            if (sel) {
                auto selSp = sel->shared_from_this();
                auto cloned = m_clipboard->clone();
                std::string baseName = cloned->Name;
                int n = 1;
                while (selSp->children.count(cloned->Name) > 0)
                    cloned->Name = baseName + std::to_string(n++);
                m_history.execute(std::make_unique<AddInstanceCommand>(selSp, cloned));
                m_isDirty = true;
            }
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
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        COMDLG_FILTERSPEC filter = { L"Scene (*.yaml;*.yml)", L"*.yaml;*.yml" };
        pfd->SetFileTypes(1, &filter);
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR wpath = nullptr;
                item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) {
                    pendingLoadPath.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, pendingLoadPath.data(), len, nullptr, nullptr);
                }
                CoTaskMemFree(wpath);
                item->Release();
            }
        }
        pfd->Release();
    }
}

void EditorManager::requestSaveDialog(GLFWwindow* window) {
    m_showSaveDialog = true;
    m_dialogWindow   = window;
}

void EditorManager::renderSaveDialog() {
    if (m_showSaveDialog) {
        ImGui::OpenPopup("Unsaved Changes");
        m_showSaveDialog = false;
    }

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("シーンに未保存の変更があります。");
        ImGui::Text("終了する前に保存しますか？");
        ImGui::Separator();

        if (ImGui::Button("保存「して」終了", ImVec2(130, 0))) {
            saveCurrentScene();
            // GL コンテキストが生きている今のうちに GPU リソースを持つ
            // インスタンスの shared_ptr を解放する（コンテキスト破棄後の
            // glDelete* 呼び出しによるヒープ破壊を防ぐ）
            hierarchyPanel->selectedInstance = nullptr;
            hierarchyPanel->selectedInstances.clear();
            m_history.clear();
            m_clipboard.reset();
            if (m_dialogWindow) glfwSetWindowShouldClose(m_dialogWindow, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存「せず」終了", ImVec2(130, 0))) {
            m_isDirty = false;
            hierarchyPanel->selectedInstance = nullptr;
            hierarchyPanel->selectedInstances.clear();
            m_history.clear();
            m_clipboard.reset();
            if (m_dialogWindow) glfwSetWindowShouldClose(m_dialogWindow, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(90, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorManager::renderPackageDialog() {
    if (m_showPackageDialog) {
        ImGui::OpenPopup("Package Game");
        m_showPackageDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Package Game", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Game Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##pkgname", m_pkgName, sizeof(m_pkgName));

        ImGui::Text("Output Directory:");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##pkgoutdir", m_pkgOutDir, sizeof(m_pkgOutDir));
        ImGui::SameLine();
        if (ImGui::Button("参照...")) {
            IFileOpenDialog* pfd = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                           CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
                DWORD opts = 0;
                pfd->GetOptions(&opts);
                pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
                if (SUCCEEDED(pfd->Show(nullptr))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(pfd->GetResult(&item))) {
                        PWSTR wpath = nullptr;
                        item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                        int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                        if (len > 1 && len <= (int)sizeof(m_pkgOutDir)) {
                            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, m_pkgOutDir, sizeof(m_pkgOutDir), nullptr, nullptr);
                        }
                        CoTaskMemFree(wpath);
                        item->Release();
                    }
                }
                pfd->Release();
            }
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(m_isPackaging || m_pkgName[0] == '\0' || m_pkgOutDir[0] == '\0');
        if (ImGui::Button("Package", ImVec2(120, 0))) {
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
        if (ImGui::Button("Close", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }

        if (m_isPackaging) {
            ImGui::SameLine();
            ImGui::Text("Processing...");
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

template <typename T, typename... Args>
void EditorManager::tryAddObject(const std::string& menuLabel, const std::string& defaultName, Args&&... args)
{
    if (ImGui::MenuItem(menuLabel.c_str()) && m_workspace) {
        Vector3 spawnPos = computeSpawnPos(m_user, m_workspace);
        
        auto obj = std::make_shared<T>(spawnPos, Vector3(1, 1, 1), std::forward<Args>(args)...);
        
        std::string name = defaultName;
        int n = 1;
        while (m_workspace->children.count(name) > 0) {
            name = defaultName + std::to_string(n++);
        }
        obj->Name = name;
        
        m_history.execute(std::make_unique<AddInstanceCommand>(
            m_workspace->shared_from_this(), obj));
        m_isDirty = true;
    }
}

void EditorManager::renderToolbar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 60.0f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 10.0f));
    ImGui::Begin("Toolbar", nullptr, tbFlags);
    ImGui::PopStyleVar();

    const ImVec4 colActive   = ImVec4(0.30f, 0.50f, 0.85f, 1.0f);
    const ImVec4 colInactive = ImVec4(0.22f, 0.40f, 0.70f, 0.60f);
    const ImVec2 btnSz       = ImVec2(70.0f, 38.0f);

    // ---- Play / Pause / Stop ----
    if (mode == EditorMode::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.18f, 1.0f));
        if (ImGui::Button("  Play  ", btnSz)) mode = EditorMode::Play;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
            mode == EditorMode::Pause ? ImVec4(0.7f, 0.55f, 0.0f, 1.0f)
                                      : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        if (ImGui::Button(" Pause ", btnSz))
            mode = (mode == EditorMode::Pause) ? EditorMode::Play : EditorMode::Pause;
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
        if (ImGui::Button("  Stop  ", btnSz)) {
            mode = EditorMode::Edit;
            if (m_user) {
                m_user->controlMode = User::ControlMode::Free;
                RCBN_LOG("[INFO] Stopped. Switched to Free Camera mode.");
            }
            else {
                RCBN_LOG("[???] User instance is null.");
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // ---- Select / Move / Resize / Rotate ----
    if (viewportPanel) {
        ImGui::PushStyleColor(ImGuiCol_Button, viewportPanel->isSelectMode() ? colActive : colInactive);
        if (ImGui::Button("Select", btnSz)) { viewportPanel->selectOnly = true; }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, viewportPanel->isMoveMode() ? colActive : colInactive);
        if (ImGui::Button("Move", btnSz)) {
            viewportPanel->selectOnly = false;
            viewportPanel->gizmoOp   = ImGuizmo::TRANSLATE;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, viewportPanel->isResizeMode() ? colActive : colInactive);
        if (ImGui::Button("Resize", btnSz)) {
            viewportPanel->selectOnly = false;
            viewportPanel->gizmoOp   = ImGuizmo::SCALE;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, viewportPanel->isRotateMode() ? colActive : colInactive);
        if (ImGui::Button("Rotate", btnSz)) {
            viewportPanel->selectOnly = false;
            viewportPanel->gizmoOp   = ImGuizmo::ROTATE;
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // ---- スナップ / 衝突フィット ----
    if (viewportPanel) {
        ImGui::Checkbox("移動スナップ##snapT", &viewportPanel->snapTranslate);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        ImGui::DragFloat("studs##snapTVal", &viewportPanel->snapTranslateVal, 0.1f, 0.1f, 100.0f, "%.1f");
        ImGui::SameLine();

        ImGui::Checkbox("回転スナップ##snapR", &viewportPanel->snapRotate);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        ImGui::DragFloat("\xc2\xb0##snapRVal", &viewportPanel->snapRotateVal, 1.0f, 1.0f, 180.0f, "%.0f");
        ImGui::SameLine();

        ImGui::Checkbox("リサイズスナップ##snapS", &viewportPanel->snapScale);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        ImGui::DragFloat("studs##snapSVal", &viewportPanel->snapScaleVal, 0.1f, 0.1f, 100.0f, "%.1f");
        ImGui::SameLine();

        ImGui::Checkbox("衝突フィット##cf", &viewportPanel->collisionFit);
        ImGui::SameLine();
    }

    ImGui::Text("|");
    ImGui::SameLine();

    // ---- Add Object ドロップダウン ----
    if (ImGui::Button("Add Object v", ImVec2(100.0f, 38.0f)))
        ImGui::OpenPopup("AddObjectPopup");

    if (ImGui::BeginPopup("AddObjectPopup")) {

        tryAddObject<Cube>("New Cube", "Cube", Cube::defaultTextureID);
        tryAddObject<Cylinder>("New Cylinder", "Cylinder");
        tryAddObject<TriangularPrism>("New Prism", "TriangularPrism");
        tryAddObject<Sphere>("New Sphere", "Sphere");

        ImGui::EndPopup();
    }

    // ---- Save / Load（右端）----
    float saveLoadW = btnSz.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - saveLoadW - 8.0f);

    if (ImGui::Button("Save", btnSz)) {
        saveCurrentScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", btnSz)) {
        IFileOpenDialog* pfd = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
            COMDLG_FILTERSPEC filter = { L"Scene (*.yaml;*.yml)", L"*.yaml;*.yml" };
            pfd->SetFileTypes(1, &filter);
            if (SUCCEEDED(pfd->Show(nullptr))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(pfd->GetResult(&item))) {
                    PWSTR wpath = nullptr;
                    item->GetDisplayName(SIGDN_FILESYSPATH, &wpath);
                    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 1) {
                        pendingLoadPath.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, pendingLoadPath.data(), len, nullptr, nullptr);
                    }
                    CoTaskMemFree(wpath);
                    item->Release();
                }
            }
            pfd->Release();
        }
    }

    ImGui::End();
}

void EditorManager::cleanupOrphanedSelection() {
    auto& si = hierarchyPanel->selectedInstances;
    si.erase(std::remove_if(si.begin(), si.end(),
        [](Instance* i){ return !i || i->Parent.expired(); }), si.end());
    if (hierarchyPanel->selectedInstance &&
        hierarchyPanel->selectedInstance->Parent.expired()) {
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
    return IsViewportFocused(viewportPanel.get());
}

Instance* EditorManager::getSelectedInstance() {
    return hierarchyPanel->selectedInstance;
}

void EditorManager::clearForImGui(GLFWwindow* window) {
    int winW, winH;
    glfwGetFramebufferSize(window, &winW, &winH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void EditorManager::renderUI(User&, GLFWwindow* window, Workspace&) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    render(window);

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
