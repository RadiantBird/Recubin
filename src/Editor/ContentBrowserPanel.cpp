#include <Editor/ContentBrowserPanel.hpp>
#include <Editor/Localization.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Instances/FileRef.hpp>
#include <include/imgui/imgui.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ===================================================
//  ContentBrowserPanel 実装
// ===================================================

ContentBrowserPanel::ContentBrowserPanel()
    : EditorPanel("Content Browser"),
      currentPath(fs::path("assets")) {}

void ContentBrowserPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(600, 180), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    // ---- パスナビゲーション ----
    ImGui::TextDisabled("%s", currentPath.string().c_str());
    if (currentPath != fs::path("assets")) {
        ImGui::SameLine();
        if (ImGui::SmallButton(Loc::t(Loc::LocKey::BackButton))) {
            currentPath = currentPath.parent_path();
        }
    }
    ImGui::Separator();

    // ---- ファイル一覧 ----
    drawDirectory(currentPath);

    ImGui::End();
}

void ContentBrowserPanel::drawDirectory(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        ImGui::TextDisabled("%s", Loc::t(Loc::LocKey::AssetsFolderNotFound));
        return;
    }

    std::vector<fs::directory_entry> dirs, files;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (entry.is_directory(ec)) dirs.push_back(entry);
        else                         files.push_back(entry);
    }

    // ディレクトリ
    for (const auto& d : dirs) {
        std::string label = "[DIR]  " + d.path().filename().string();
        if (ImGui::Selectable(label.c_str(), false)) {
            currentPath = d.path();
        }
    }

    // ファイル
    for (const auto& f : files) {
        std::string ext  = f.path().extension().string();
        std::string icon = "       ";
        if (ext == ".yaml" || ext == ".yml")                       icon = "[SCN]  ";
        else if (ext == ".png"  || ext == ".jpg" || ext == ".jpeg"
              || ext == ".bmp")                                     icon = "[IMG]  ";
        else if (ext == ".lua"  || ext == ".luau")                  icon = "[LUA]  ";
        else if (ext == ".glsl")                                    icon = "[GLSL] ";

        std::string label = icon + f.path().filename().string();
        ImGui::Selectable(label.c_str(), false);

        // ---- 右クリックコンテキストメニュー ----
        std::string popupId = "cb_ctx##" + f.path().generic_string();
        if (ImGui::BeginPopupContextItem(popupId.c_str())) {
            // FileRefを生成: 選択インスタンス（無ければWorkspace）の子として undo 対応で挿入
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::CreateFileRef))) {
                std::shared_ptr<Instance> parent;
                if (selectedInstance && *selectedInstance) parent = (*selectedInstance)->shared_from_this();
                else if (workspace && *workspace)          parent = (*workspace)->shared_from_this();
                if (parent && m_history) {
                    auto fr = std::make_shared<FileRef>();
                    fr->Path = f.path().generic_string();  // "assets/..." 相対。YAML では ContentPath になり Packager が追跡する
                    fr->Name = SceneHierarchyPanel::uniqueName(parent, f.path().stem().string());
                    m_history->execute(std::make_unique<AddInstanceCommand>(parent, fr));
                }
            }
            // パスをコピー
            if (ImGui::MenuItem(Loc::t(Loc::LocKey::CopyPath))) {
                ImGui::SetClipboardText(f.path().generic_string().c_str());
            }
            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", f.path().string().c_str());
        }
    }
}
