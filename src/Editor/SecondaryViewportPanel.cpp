#include <Editor/SecondaryViewportPanel.hpp>
#include <Core/Renderer.hpp>
#include <include/imgui/imgui.h>
#include <cmath>
#include <string>

SecondaryViewportPanel::SecondaryViewportPanel(std::weak_ptr<Workspace> ws, const std::string& title)
    : m_workspace(std::move(ws)), m_title(title) {
    initFBO(m_fbWidth, m_fbHeight);
}

SecondaryViewportPanel::~SecondaryViewportPanel() {
    destroyFBO();
}

void SecondaryViewportPanel::initFBO(int w, int h) {
    destroyFBO();
    m_fbWidth  = w;
    m_fbHeight = h;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

    glGenRenderbuffers(1, &m_depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SecondaryViewportPanel::resizeFBO(int w, int h) {
    if (w == m_fbWidth && h == m_fbHeight) return;
    initFBO(w, h);
}

void SecondaryViewportPanel::destroyFBO() {
    if (m_fbo)      { glDeleteFramebuffers(1,  &m_fbo);      m_fbo      = 0; }
    if (m_colorTex) { glDeleteTextures(1,      &m_colorTex); m_colorTex = 0; }
    if (m_depthRbo) { glDeleteRenderbuffers(1, &m_depthRbo); m_depthRbo = 0; }
}

Quaternion SecondaryViewportPanel::getCamRot() const {
    Quaternion qYaw   = Quaternion::fromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), m_yaw);
    Quaternion qPitch = Quaternion::fromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), m_pitch);
    return qYaw * qPitch;
}

void SecondaryViewportPanel::onRender() {
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(680.0f, 420.0f), ImGuiCond_FirstUseEver);
    std::string winTitle = m_title + "###SecVP_" + m_title;
    if (!ImGui::Begin(winTitle.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    // ---- FBO リサイズ ----
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int newW = (int)avail.x;
    int newH = (int)avail.y;
    if (newW < 16) newW = 16;
    if (newH < 16) newH = 16;
    resizeFBO(newW, newH);

    // ---- カメラ入力 ----
    bool hovered = ImGui::IsWindowHovered();
    bool focused = ImGui::IsWindowFocused();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        ImGui::SetWindowFocus();
        float dx = io.MouseDelta.x * 0.3f;
        float dy = io.MouseDelta.y * 0.3f;
        m_yaw   -= dx;
        m_pitch -= dy;
        if (m_pitch >  89.0f) m_pitch =  89.0f;
        if (m_pitch < -89.0f) m_pitch = -89.0f;
    }

    // フォーカスされているビューポートではキーボード移動を有効にする
    if (focused) {
        float dt    = io.DeltaTime;
        Quaternion q = getCamRot();
        Vector3 fwd   = q.getForward();
        Vector3 right = q.getRight();
        float spd = m_speed * dt;
        if (ImGui::IsKeyDown(ImGuiKey_W)) m_camPos = m_camPos + fwd   *  spd;
        if (ImGui::IsKeyDown(ImGuiKey_S)) m_camPos = m_camPos + fwd   * -spd;
        if (ImGui::IsKeyDown(ImGuiKey_D)) m_camPos = m_camPos + right *  spd;
        if (ImGui::IsKeyDown(ImGuiKey_A)) m_camPos = m_camPos + right * -spd;
        if (ImGui::IsKeyDown(ImGuiKey_E)) m_camPos.y += spd;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) m_camPos.y -= spd;
    }

    // ---- Workspaceを取得してFBOに描画 ----
    auto wsSp = m_workspace.lock();
    if (wsSp && Renderer::instance) {
        ViewportRenderDesc desc;
        desc.fbo = m_fbo;
        desc.width = m_fbWidth;
        desc.height = m_fbHeight;
        desc.cameraPosition = m_camPos;
        Quaternion camRot = getCamRot();
        desc.cameraForward = camRot.getForward();
        desc.cameraUp = camRot.getUp();
        desc.workspace = wsSp.get();
        desc.renderShadows = true;
        desc.renderHighlights = false;
        desc.renderConstraints = false;
        desc.isFocused = focused;

        Renderer::instance->renderViewport(desc);
    }

    // ---- FBOテクスチャを表示 ----
    ImGui::Image((ImTextureID)(intptr_t)m_colorTex,
                 ImVec2((float)m_fbWidth, (float)m_fbHeight),
                 ImVec2(0, 1), ImVec2(1, 0));

    ImGui::End();
}
