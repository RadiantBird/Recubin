#include <Editor/EditorManager.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Instances/Cube.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <string>

// ===================================================
//  EditorManager 実装
// ===================================================

EditorManager::EditorManager(Workspace* workspace, User* user) {
    m_workspace = workspace;

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

    applyTheme();
}

void EditorManager::render() {
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
            ImGui::MenuItem("Open Scene",  "Ctrl+O");
            ImGui::MenuItem("Save Scene",  "Ctrl+S");
            ImGui::Separator();
            ImGui::MenuItem("Quit",        "Alt+F4");
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
        if (ImGui::Button("  Stop  ", btnSz)) mode = EditorMode::Edit;
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

    // ---- New Cube ----
    if (ImGui::Button("New Cube", btnSz) && m_workspace) {
        auto* cube = new Cube(Vector3(0, 5, 0), Vector3(1, 1, 1), Cube::defaultTextureID);
        std::string name = "Cube";
        int n = 1;
        while (m_workspace->children.count(name) > 0)
            name = "Cube" + std::to_string(n++);
        cube->Name = name;
        m_workspace->addChild(cube);
    }

    // ---- Save / Load（右端）----
    float saveLoadW = btnSz.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - saveLoadW - 8.0f);

    ImGui::Button("Save", btnSz);
    ImGui::SameLine();
    ImGui::Button("Load", btnSz);

    ImGui::End();
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

    // ベースカラー：深い青
    c[ImGuiCol_WindowBg]          = ImVec4(0.11f, 0.17f, 0.40f, 1.0f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.09f, 0.14f, 0.35f, 1.0f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.09f, 0.14f, 0.35f, 0.98f);
    c[ImGuiCol_Border]            = ImVec4(0.25f, 0.27f, 0.35f, 1.0f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);

    // ヘッダー・選択
    c[ImGuiCol_Header]            = ImVec4(0.22f, 0.40f, 0.70f, 0.55f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.30f, 0.50f, 0.85f, 0.70f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.25f, 0.45f, 0.80f, 1.0f);

    // ボタン
    c[ImGuiCol_Button]            = ImVec4(0.22f, 0.40f, 0.70f, 0.60f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.30f, 0.50f, 0.85f, 0.80f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.20f, 0.38f, 0.70f, 1.0f);

    // フレーム
    c[ImGuiCol_FrameBg]           = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.20f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.24f, 0.28f, 0.38f, 1.0f);

    // タブ
    c[ImGuiCol_Tab]               = ImVec4(0.08f, 0.12f, 0.32f, 1.0f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.18f, 0.32f, 0.68f, 1.0f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.20f, 0.40f, 0.82f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.50f, 0.75f, 1.0f, 1.0f);

    // タイトルバー
    c[ImGuiCol_TitleBg]           = ImVec4(0.05f, 0.10f, 0.28f, 1.0f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.20f, 0.52f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.05f, 0.10f, 0.28f, 0.8f);

    // テキスト
    c[ImGuiCol_Text]              = ImVec4(0.90f, 0.92f, 0.95f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.48f, 0.55f, 1.0f);

    // スクロールバー
    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.30f, 0.35f, 0.45f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.44f, 0.56f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.44f, 0.52f, 0.68f, 1.0f);

    // セパレーター
    c[ImGuiCol_Separator]         = ImVec4(0.25f, 0.27f, 0.35f, 1.0f);
    c[ImGuiCol_SeparatorHovered]  = ImVec4(0.40f, 0.60f, 0.90f, 0.78f);
    c[ImGuiCol_SeparatorActive]   = ImVec4(0.40f, 0.60f, 0.90f, 1.0f);

    // ドックスプリッター
    c[ImGuiCol_DockingPreview]    = ImVec4(0.30f, 0.55f, 0.95f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]    = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
}
