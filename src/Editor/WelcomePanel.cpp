#include <Editor/WelcomePanel.hpp>
#include <Editor/Localization.hpp>
#include <filesystem>

// ===================================================
//  WelcomePanel 実装
// ===================================================

WelcomePanel::WelcomePanel() : EditorPanel("###Welcome") {}

void WelcomePanel::onRender() {
    if (dockspaceId != 0) ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    ImGui::Spacing();
    ImGui::TextWrapped("%s", Loc::t(Loc::LocKey::WelcomeMessage));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(Loc::t(Loc::LocKey::WelcomeBtnNew), ImVec2(240, 0))) {
        if (onNewScene) onNewScene();
        isOpen = false;
    }

    bool canContinue = !lastScenePath.empty() && std::filesystem::exists(lastScenePath);
    if (!canContinue) ImGui::BeginDisabled();
    if (ImGui::Button(Loc::t(Loc::LocKey::WelcomeBtnContinue), ImVec2(240, 0))) {
        if (onLoadLast) onLoadLast();
        isOpen = false;
    }
    if (!canContinue) ImGui::EndDisabled();
    if (canContinue) ImGui::SetItemTooltip("%s", lastScenePath.c_str());

    if (ImGui::Button(Loc::t(Loc::LocKey::WelcomeBtnOpen), ImVec2(240, 0))) {
        if (onOpenScene && onOpenScene()) isOpen = false;
    }

    ImGui::End();
}
