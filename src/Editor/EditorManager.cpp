#include <Editor/EditorManager.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Core/SceneLoader.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>
#include <string>

// ===================================================
//  EditorManager 実装
// ===================================================

EditorManager::EditorManager(Workspace* workspace, User* user) {
    m_workspace = workspace;
    m_user      = user;

    consolePanel        = std::make_unique<ConsolePanel>();
    hierarchyPanel      = std::make_unique<SceneHierarchyPanel>();
    propertiesPanel     = std::make_unique<PropertiesPanel>();
    contentBrowserPanel = std::make_unique<ContentBrowserPanel>();
    viewportPanel       = std::make_unique<ViewportPanel>();

    hierarchyPanel->workspace  = workspace;
    viewportPanel->user        = user;
    viewportPanel->workspace   = workspace;

    // selectedInstance ポインタを共有（SceneHierarchy が書き、Properties/Viewport が読む）
    propertiesPanel->selectedInstance = &hierarchyPanel->selectedInstance;
    viewportPanel->selectedInstance   = &hierarchyPanel->selectedInstance;

    // CommandHistory と clipboard を各パネルに渡す
    hierarchyPanel->m_history   = &m_history;
    hierarchyPanel->m_clipboard = &m_clipboard;
    propertiesPanel->m_history  = &m_history;
    viewportPanel->m_history    = &m_history;

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
            ImGui::MenuItem("Open Scene",  "Ctrl+O");
            ImGui::Separator();
            ImGui::MenuItem("Quit", "Alt+F4");
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
}

void EditorManager::handleEditorShortcuts() {
    // テキスト入力中はショートカットをスキップ
    bool textActive = ImGui::GetIO().WantTextInput;

    // Ctrl+S: 保存
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        saveCurrentScene();
        return;
    }

    if (!textActive) {
        // Ctrl+Z: Undo
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            m_history.undo();
        }
        // Ctrl+Shift+Z: Redo
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
            m_history.redo();
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
                    hierarchyPanel->selectedInstance = nullptr;
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
    if (!m_workspace) return;
    SceneLoader::saveScene(m_workspace, scenePath);
    m_isDirty = false;
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
            if (m_dialogWindow) glfwSetWindowShouldClose(m_dialogWindow, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存「せず」終了", ImVec2(130, 0))) {
            m_isDirty = false;
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
            if (m_user) m_user->controlMode = User::ControlMode::Free;
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // ---- Select / Move / Resize / Rotate ----
    if (viewportPanel) {
        bool& selectOnly = viewportPanel->selectOnly;
        ImGuizmo::OPERATION& op = viewportPanel->gizmoOp;

        ImGui::PushStyleColor(ImGuiCol_Button, selectOnly ? colActive : colInactive);
        if (ImGui::Button("Select", btnSz)) { selectOnly = true; }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            (!selectOnly && op == ImGuizmo::TRANSLATE) ? colActive : colInactive);
        if (ImGui::Button("Move", btnSz)) { selectOnly = false; op = ImGuizmo::TRANSLATE; }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            (!selectOnly && op == ImGuizmo::SCALE) ? colActive : colInactive);
        if (ImGui::Button("Resize", btnSz)) { selectOnly = false; op = ImGuizmo::SCALE; }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,
            (!selectOnly && op == ImGuizmo::ROTATE) ? colActive : colInactive);
        if (ImGui::Button("Rotate", btnSz)) { selectOnly = false; op = ImGuizmo::ROTATE; }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // ---- New Cube（CommandHistory経由） ----
    if (ImGui::Button("New Cube", btnSz) && m_workspace) {
        auto cube = std::make_shared<Cube>(Vector3(0, 5, 0), Vector3(1, 1, 1), Cube::defaultTextureID);
        std::string name = "Cube";
        int n = 1;
        while (m_workspace->children.count(name) > 0)
            name = "Cube" + std::to_string(n++);
        cube->Name = name;
        m_history.execute(std::make_unique<AddInstanceCommand>(
            m_workspace->shared_from_this(), cube));
        m_isDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Cylinder", btnSz) && m_workspace) {
        auto obj = std::make_shared<Cylinder>(Vector3(0, 5, 0), Vector3(1, 1, 1));
        std::string name = "Cylinder";
        int n = 1;
        while (m_workspace->children.count(name) > 0)
            name = "Cylinder" + std::to_string(n++);
        obj->Name = name;
        m_history.execute(std::make_unique<AddInstanceCommand>(
            m_workspace->shared_from_this(), obj));
        m_isDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Prism", btnSz) && m_workspace) {
        auto obj = std::make_shared<TriangularPrism>(Vector3(0, 5, 0), Vector3(1, 1, 1));
        std::string name = "TriangularPrism";
        int n = 1;
        while (m_workspace->children.count(name) > 0)
            name = "TriangularPrism" + std::to_string(n++);
        obj->Name = name;
        m_history.execute(std::make_unique<AddInstanceCommand>(
            m_workspace->shared_from_this(), obj));
        m_isDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Sphere", btnSz) && m_workspace) {
        auto obj = std::make_shared<Sphere>(Vector3(0, 5, 0), Vector3(1, 1, 1));
        std::string name = "Sphere";
        int n = 1;
        while (m_workspace->children.count(name) > 0)
            name = "Sphere" + std::to_string(n++);
        obj->Name = name;
        m_history.execute(std::make_unique<AddInstanceCommand>(
            m_workspace->shared_from_this(), obj));
        m_isDirty = true;
    }

    // ---- Save / Load（右端）----
    float saveLoadW = btnSz.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - saveLoadW - 8.0f);

    if (ImGui::Button("Save", btnSz)) {
        saveCurrentScene();
    }
    ImGui::SameLine();
    ImGui::Button("Load", btnSz);

    ImGui::End();
}

void EditorManager::setWorkspace(Workspace* ws) {
    m_workspace                  = ws;
    hierarchyPanel->workspace    = ws;
    viewportPanel->workspace     = ws;
    hierarchyPanel->selectedInstance = nullptr;
    m_history.clear();
    m_isDirty = false;
}

void EditorManager::beginViewportRender() {
    viewportPanel->beginRender();
}

void EditorManager::endViewportRender() {
    viewportPanel->endRenderAndDisplay();
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
