#include <Editor/WelcomePanel.hpp>
#include <Editor/Localization.hpp>
#include <include/stb_image.h>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows26.h>

namespace {
constexpr int RECUBIN_WELCOME_LOGO = 101;
}
#endif

// ===================================================
//  WelcomePanel 実装
// ===================================================

WelcomePanel::WelcomePanel() : EditorPanel("###Welcome") {}

WelcomePanel::~WelcomePanel() {
    if (m_logoTexture != 0) glDeleteTextures(1, &m_logoTexture);
}

void WelcomePanel::loadLogo() {
    if (m_logoLoadAttempted) return;
    m_logoLoadAttempted = true;

#ifdef _WIN32
    HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(RECUBIN_WELCOME_LOGO), RT_RCDATA);
    HGLOBAL loadedResource = resource ? LoadResource(nullptr, resource) : nullptr;
    const auto* resourceData = loadedResource
        ? static_cast<const stbi_uc*>(LockResource(loadedResource)) : nullptr;
    const DWORD resourceSize = resource ? SizeofResource(nullptr, resource) : 0;
    if (!resourceData || resourceSize == 0) return;

    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(resourceData, static_cast<int>(resourceSize),
        &m_logoWidth, &m_logoHeight, &channels, 4);
    if (!pixels || m_logoWidth <= 0 || m_logoHeight <= 0) {
        if (pixels) stbi_image_free(pixels);
        m_logoWidth = m_logoHeight = 0;
        return;
    }

    glGenTextures(1, &m_logoTexture);
    glBindTexture(GL_TEXTURE_2D, m_logoTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_logoWidth, m_logoHeight,
        0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
#else
#endif
}

void WelcomePanel::onRender() {
    if (dockspaceId != 0) ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    loadLogo();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float logoWidth = m_logoTexture != 0
        ? std::min(500.0f, std::max(0.0f, available.x)) : 0.0f;
    const float logoHeight = logoWidth * static_cast<float>(m_logoHeight) /
        static_cast<float>(m_logoWidth == 0 ? 1 : m_logoWidth);
    constexpr float buttonWidth = 240.0f;
    constexpr float preferredCardWidth = 272.0f;
    const float cardWidth = std::max(buttonWidth, std::min(preferredCardWidth, available.x));
    const float groupWidth = std::max(cardWidth, logoWidth);
    const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
    const ImVec2 messageSize = ImGui::CalcTextSize(Loc::t(Loc::LocKey::WelcomeMessage),
        nullptr, false, std::max(0.0f, cardWidth - framePadding.x * 2.0f));
    const float itemSpacing = ImGui::GetStyle().ItemSpacing.y;
    const float buttonHeight = ImGui::GetFrameHeight();
    const float cardHeight = framePadding.y * 2.0f + messageSize.y
        + buttonHeight * 3.0f + itemSpacing * 3.0f;
    const float groupHeight = (logoHeight > 0.0f ? logoHeight + itemSpacing : 0.0f)
        + cardHeight;
    ImGui::SetCursorPos(ImVec2(
        ImGui::GetCursorPosX() + std::max(0.0f, (available.x - groupWidth) * 0.5f),
        ImGui::GetCursorPosY() + std::max(0.0f, (available.y - groupHeight) * 0.5f)));

    ImGui::BeginGroup();
    if (m_logoTexture != 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (groupWidth - logoWidth) * 0.5f);
        ImGui::Image(static_cast<ImTextureID>(m_logoTexture),
            ImVec2(logoWidth, logoHeight), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (groupWidth - cardWidth) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.16f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.34f, 0.55f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    if (ImGui::BeginChild("##WelcomeActions", ImVec2(cardWidth, cardHeight),
        ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::SetCursorPosX(std::max(framePadding.x, (cardWidth - messageSize.x) * 0.5f));
        ImGui::TextWrapped("%s", Loc::t(Loc::LocKey::WelcomeMessage));
        const float buttonStartX = std::max(framePadding.x, (cardWidth - buttonWidth) * 0.5f);
        ImGui::SetCursorPosX(buttonStartX);

        if (ImGui::Button(Loc::t(Loc::LocKey::WelcomeBtnNew), ImVec2(buttonWidth, 0))) {
            if (onNewScene) onNewScene();
            isOpen = false;
        }

        bool canContinue = !lastScenePath.empty() && std::filesystem::exists(lastScenePath);
        ImGui::SetCursorPosX(buttonStartX);
        if (!canContinue) ImGui::BeginDisabled();
        if (ImGui::Button(Loc::t(Loc::LocKey::WelcomeBtnContinue), ImVec2(buttonWidth, 0))) {
            if (onLoadLast) onLoadLast();
            isOpen = false;
        }
        if (!canContinue) ImGui::EndDisabled();
        if (canContinue) ImGui::SetItemTooltip("%s", lastScenePath.c_str());

        ImGui::SetCursorPosX(buttonStartX);
        if (ImGui::Button(Loc::t(Loc::LocKey::WelcomeBtnOpen), ImVec2(buttonWidth, 0))) {
            if (onOpenScene && onOpenScene()) isOpen = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::EndGroup();
    ImGui::End();
}
