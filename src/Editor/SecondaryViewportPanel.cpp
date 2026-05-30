#include <Editor/SecondaryViewportPanel.hpp>
#include <Core/Renderer.hpp>
#include <Core/User.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Math/Matrix4.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <cmath>
#include <string>
#include <algorithm>

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

    // コンテンツ領域の画面座標原点
    ImVec2 contentOrigin;
    {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 cm = ImGui::GetWindowContentRegionMin();
        contentOrigin = ImVec2(wp.x + cm.x, wp.y + cm.y);
    }

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

    // ===================================================
    //  ギズモ・レイキャスト選択（編集ポインタが渡されている場合のみ）
    // ===================================================
    Workspace* ws = wsSp.get();
    bool hasEditCtx = selectedInstance && selectedInstances && gizmoOp && selectOnly && ws;

    if (hasEditCtx) {
        int w = m_fbWidth;
        int h = m_fbHeight;

        Quaternion camRot = getCamRot();
        Vector3 camFwd   = camRot.getForward();
        Vector3 camRight = camRot.getRight();
        Vector3 camUp    = camRot.getUp();

        // ワールド座標 → Spatial ローカル座標
        auto worldToLocal = [](const Vector3& worldPos, const Spatial* sp) -> Vector3 {
            auto par = sp->Parent.lock();
            if (par && par->IsA("Spatial")) {
                CFrame pw = static_cast<Spatial*>(par.get())->getWorldCFrame();
                return pw.Rotation.conjugate().rotate(worldPos - pw.Position);
            }
            return worldPos;
        };

        // ワールド回転 → ローカル回転
        auto worldToLocalRot = [](const Quaternion& worldRot, const Spatial* sp) -> Quaternion {
            auto par = sp->Parent.lock();
            if (par && par->IsA("Spatial")) {
                CFrame pw = static_cast<Spatial*>(par.get())->getWorldCFrame();
                return pw.Rotation.conjugate() * worldRot;
            }
            return worldRot;
        };

        // マウスパネル座標 → レイ方向
        auto makeRay = [&](float mx, float my) -> Vector3 {
            float ndcX  = (mx / (float)w) * 2.0f - 1.0f;
            float ndcY  = 1.0f - (my / (float)h) * 2.0f;
            float aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.0f;
            float tanH  = std::tan(45.0f * (3.14159265f / 180.0f) * 0.5f);
            return (camFwd
                  + camRight * (ndcX * aspect * tanH)
                  + camUp    * (ndcY * tanH)).normalize();
        };

        // OBB レイキャスト
        auto obbHit = [](const Vector3& ori, const Vector3& dir,
                         const CFrame& worldCF, const Vector3& size) -> float {
            Quaternion invRot = worldCF.Rotation.conjugate();
            Vector3 lo = invRot.rotate(ori - worldCF.Position);
            Vector3 ld = invRot.rotate(dir);
            float ld3[3] = { ld.x, ld.y, ld.z };
            float lo3[3] = { lo.x, lo.y, lo.z };
            float hs[3]  = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
            float tmin = -1e30f, tmax = 1e30f;
            for (int i = 0; i < 3; ++i) {
                if (std::abs(ld3[i]) < 1e-8f) {
                    if (lo3[i] < -hs[i] || lo3[i] > hs[i]) return -1.0f;
                } else {
                    float t1 = (-hs[i] - lo3[i]) / ld3[i];
                    float t2 = ( hs[i] - lo3[i]) / ld3[i];
                    if (t1 > t2) std::swap(t1, t2);
                    tmin = (std::max)(tmin, t1);
                    tmax = (std::min)(tmax, t2);
                    if (tmax < tmin) return -1.0f;
                }
            }
            if (tmax < 0.0f) return -1.0f;
            return (tmin >= 0.0f) ? tmin : tmax;
        };

        // ---- 左クリックでレイキャスト選択 ----
        if (hovered && ImGui::IsMouseClicked(0) && !ImGuizmo::IsUsing()) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float rx = mousePos.x - contentOrigin.x;
            float ry = mousePos.y - contentOrigin.y;
            Vector3 rayDir = makeRay(rx, ry);
            Vector3 rayOri = m_camPos;

            Instance* nearest = nullptr;
            float nearestT = 1e30f;
            auto castRay = [&](auto& self, Instance* inst) -> void {
                if (!inst) return;
                if (inst->GetClassName() == "Skybox") return;
                if (inst->IsA("BaseCube")) {
                    Spatial* s = static_cast<Spatial*>(inst);
                    float t = obbHit(rayOri, rayDir, s->getWorldCFrame(), s->Size);
                    if (t >= 0.0f && t < nearestT) { nearestT = t; nearest = inst; }
                }
                for (auto const& [_, child] : inst->getChildren())
                    self(self, child.get());
            };
            castRay(castRay, ws);
            *selectedInstance = nearest;
            selectedInstances->clear();
            if (nearest) selectedInstances->push_back(nearest);
        }

        // ---- F キー: 選択オブジェクトにカメラをフォーカス ----
        if (focused && selectedInstance && *selectedInstance
                && ImGui::IsKeyPressed(ImGuiKey_F)) {
            Instance* inst = *selectedInstance;
            if (inst->IsA("Spatial")) {
                Spatial* s = static_cast<Spatial*>(inst);
                Vector3 objPos = s->getWorldPosition();
                float maxSize = (std::max)(s->Size.x, (std::max)(s->Size.y, s->Size.z));
                float dist = (std::max)(maxSize * 3.0f, 5.0f);
                Quaternion camRotNow = getCamRot();
                m_camPos = objPos - camRotNow.getForward() * dist;
            }
        }

        // ---- ギズモのオーバーレイ ----
        if (!(*selectOnly) && selectedInstance && *selectedInstance) {
            Instance* inst = *selectedInstance;
            if (inst->IsA("Spatial")) {
                Spatial* s = static_cast<Spatial*>(inst);

                // ギズモ Undo 検知
                bool isUsingGizmo = ImGuizmo::IsUsing();

                if (!m_wasUsingGizmo && isUsingGizmo) {
                    m_gizmoEntries.clear();
                    std::vector<Instance*> targets = (selectedInstances->size() > 1)
                        ? *selectedInstances : std::vector<Instance*>{ inst };
                    for (Instance* tgt : targets) {
                        if (tgt && !tgt->Parent.expired() && tgt->IsA("BaseCube")) {
                            BaseCube* bc = static_cast<BaseCube*>(tgt);
                            m_gizmoEntries.push_back({
                                std::static_pointer_cast<BaseCube>(tgt->shared_from_this()),
                                { bc->Position, bc->Size, bc->Rotation }, {}
                            });
                        }
                    }
                }
                if (m_wasUsingGizmo && !isUsingGizmo && m_history && !m_gizmoEntries.empty()) {
                    for (auto& e : m_gizmoEntries) {
                        if (e.target && !e.target->Parent.expired())
                            e.after = { e.target->Position, e.target->Size, e.target->Rotation };
                    }
                    m_history->record(std::make_unique<MultiGizmoCommand>(std::move(m_gizmoEntries)));
                    m_gizmoEntries.clear();
                }
                m_wasUsingGizmo = isUsingGizmo;

                float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
                Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
                Vector3 camTarget = m_camPos + camFwd;
                Matrix4 view = Matrix4::LookAt(m_camPos, camTarget, camUp);

                Matrix4 model = s->getWorldCFrame().toMatrix4() *
                                Matrix4::Scale(s->Size.x, s->Size.y, s->Size.z);

                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(contentOrigin.x, contentOrigin.y, (float)w, (float)h);

                float snapArr[3] = { 1.0f, 1.0f, 1.0f };
                float rotSnap[3] = { 15.0f, 15.0f, 15.0f };
                if (snapTranslateVal) { snapArr[0] = snapArr[1] = snapArr[2] = *snapTranslateVal; }
                if (snapRotateVal)    { rotSnap[0] = rotSnap[1] = rotSnap[2] = *snapRotateVal; }
                const float* snap = nullptr;
                if      (*gizmoOp == ImGuizmo::TRANSLATE && snapTranslate && *snapTranslate) snap = snapArr;
                else if (*gizmoOp == ImGuizmo::ROTATE    && snapRotate    && *snapRotate)    snap = rotSnap;

                if (ImGuizmo::Manipulate(view.m, proj.m, *gizmoOp,
                                         ImGuizmo::WORLD, model.m, nullptr, snap)) {
                    float sx = std::sqrt(model.m[0]*model.m[0] + model.m[1]*model.m[1] + model.m[2]*model.m[2]);
                    float sy = std::sqrt(model.m[4]*model.m[4] + model.m[5]*model.m[5] + model.m[6]*model.m[6]);
                    float sz = std::sqrt(model.m[8]*model.m[8] + model.m[9]*model.m[9] + model.m[10]*model.m[10]);
                    Vector3 newPos(model.m[12], model.m[13], model.m[14]);
                    Vector3 newSize((std::max)(sx, 0.05f), (std::max)(sy, 0.05f), (std::max)(sz, 0.05f));

                    float rotM[16] = {0};
                    if (sx > 1e-6f) { rotM[0]=model.m[0]/sx; rotM[1]=model.m[1]/sx; rotM[2]=model.m[2]/sx; }
                    if (sy > 1e-6f) { rotM[4]=model.m[4]/sy; rotM[5]=model.m[5]/sy; rotM[6]=model.m[6]/sy; }
                    if (sz > 1e-6f) { rotM[8]=model.m[8]/sz; rotM[9]=model.m[9]/sz; rotM[10]=model.m[10]/sz; }
                    rotM[15] = 1.0f;
                    Quaternion newRot = Quaternion::FromRotationMatrix(rotM);

                    if (*gizmoOp == ImGuizmo::TRANSLATE) {
                        Vector3 localPos = worldToLocal(newPos, s);
                        if (inst->IsA("BaseCube"))
                            static_cast<BaseCube*>(inst)->teleportTo(localPos);
                        else
                            s->Position = localPos;
                    } else if (*gizmoOp == ImGuizmo::SCALE) {
                        if (inst->IsA("BaseCube")) {
                            BaseCube* bc = static_cast<BaseCube*>(inst);
                            bc->setSize(newSize);
                        } else {
                            s->Size = newSize;
                        }
                    } else if (*gizmoOp == ImGuizmo::ROTATE) {
                        Quaternion localRot = worldToLocalRot(newRot, s);
                        if (inst->IsA("BaseCube"))
                            static_cast<BaseCube*>(inst)->setRotation(localRot);
                        else
                            s->cframe.Rotation = localRot;
                    }
                }
            }
        }
    }

    ImGui::End();
}
