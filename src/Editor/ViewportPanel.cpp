#include <Editor/ViewportPanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Math/Matrix4.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/BaseCube.hpp>

// ===================================================
//  ViewportPanel 実装
// ===================================================

ViewportPanel::ViewportPanel()
    : EditorPanel("Viewport") {
    initFBO(fbWidth, fbHeight);
}

ViewportPanel::~ViewportPanel() {
    destroyFBO();
}

void ViewportPanel::initFBO(int w, int h) {
    fbWidth  = w;
    fbHeight = h;

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // カラーテクスチャ
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTexture, 0);

    // 深度・ステンシル用レンダーバッファ
    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depthRenderbuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ViewportPanel::resizeFBO(int w, int h) {
    if (w == fbWidth && h == fbHeight) return;
    destroyFBO();
    initFBO(w, h);
}

void ViewportPanel::destroyFBO() {
    if (colorTexture)      { glDeleteTextures(1, &colorTexture);          colorTexture = 0; }
    if (depthRenderbuffer) { glDeleteRenderbuffers(1, &depthRenderbuffer); depthRenderbuffer = 0; }
    if (framebuffer)       { glDeleteFramebuffers(1, &framebuffer);        framebuffer = 0; }
}

void ViewportPanel::beginRender() {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, fbWidth, fbHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ViewportPanel::endRenderAndDisplay() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ViewportPanel::onRender() {
    // パディングを削除してゲームビューをパネルに密着させる
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title.c_str(), &isOpen,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    // パネルサイズに合わせて FBO をリサイズ
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x;
    int h = (int)avail.y;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    resizeFBO(w, h);

    // FBO のカラーテクスチャを表示
    // ImTextureRef で GLuint を包む（v1.92 以降の API）
    ImTextureRef texRef((ImTextureID)(uintptr_t)colorTexture);
    ImGui::Image(texRef, avail, ImVec2(0, 1), ImVec2(1, 0)); // Y 反転

    // ===================================================
    //  Viewportクリック検出とフォーカス管理
    // ===================================================
    // Viewport領域内でのマウスホバー状態を更新
    isHoveringViewport = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    
    // Viewport外でクリックされたらフォーカスを解除
    if (!isHoveringViewport && ImGui::IsMouseClicked(0)) {
        if (isViewportFocused) {
            ViewportFocusManager::getInstance().clearFocus();
        }
    }

    // Viewportがクリックされたときにフォーカスを設定（排他制御）
    if (isHoveringViewport && ImGui::IsMouseClicked(0)) {
        ViewportFocusManager::getInstance().onFocusViewport(this);
    }

    if (isHoveringViewport && ImGui::GetIO().MouseWheel != 0.0f) {
        ViewportFocusManager::getInstance().onFocusViewport(this);
    }
    
    // フォーカス状態の可視化（フォーカス時に薄いボーダーを描画）
    if (isViewportFocused) {
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetWindowPos(),
            ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                   ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
            IM_COL32(0, 200, 255, 100),  // 薄いシアン色
            0.0f,
            0,
            2.0f
        );
    }

    // ギズモのオーバーレイ
    if (selectedInstance && *selectedInstance && user) {
        Instance* inst = *selectedInstance;
        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);

            float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
            Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
            Vector3 target = user->cpos + user->forward;
            Matrix4 view   = Matrix4::LookAt(user->cpos, target, user->up);

            Matrix4 model = s->cframe.toMatrix4() *
                            Matrix4::Scale(s->Size.x, s->Size.y, s->Size.z);

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            ImVec2 winPos = ImGui::GetWindowPos();
            ImGuizmo::SetRect(winPos.x, winPos.y, (float)w, (float)h);

            if (ImGuizmo::Manipulate(view.m, proj.m, gizmoOp,
                                     ImGuizmo::WORLD, model.m)) {
                Vector3 newPos(model.m[12], model.m[13], model.m[14]);
                if (inst->IsA("BaseCube")) {
                    static_cast<BaseCube*>(inst)->teleportTo(newPos);
                } else {
                    s->Position = newPos;
                }
            }
        }
    }

    ImGui::PopStyleVar();
    ImGui::End();
}
