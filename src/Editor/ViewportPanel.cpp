#include <Editor/ViewportPanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Math/Matrix4.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <algorithm>
#include <cmath>

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
    isHoveringViewport = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    if (!isHoveringViewport && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
        if (isViewportFocused) {
            ViewportFocusManager::getInstance().clearFocus();
        }
    }

    if (isHoveringViewport && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
        ViewportFocusManager::getInstance().onFocusViewport(this);
        ImGui::SetWindowFocus();
    }

    if (isHoveringViewport && ImGui::GetIO().MouseWheel != 0.0f) {
        ViewportFocusManager::getInstance().onFocusViewport(this);
    }

    // ===================================================
    //  共用ヘルパーラムダ
    // ===================================================

    // マウスパネル座標 → ワールドレイ方向
    auto makeRay = [&](float mx, float my) -> Vector3 {
        float ndcX  = (mx / (float)w) * 2.0f - 1.0f;
        float ndcY  = 1.0f - (my / (float)h) * 2.0f;
        float aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.0f;
        float tanH  = std::tan(45.0f * (3.14159265f / 180.0f) * 0.5f);
        return (user->forward
              + user->right * (ndcX * aspect * tanH)
              + user->up    * (ndcY * tanH)).normalize();
    };

    // AABB スラブ法レイキャスト
    // 最近傍の Spatial* を返し、衝突軸(0-2)と法線符号(+1/-1)を出力する
    auto castRaySurface = [&](const Vector3& ori, const Vector3& dir,
                              Instance* exclude,
                              int& outAxis, float& outSign) -> Spatial* {
        float nearestT = 1e30f;
        Spatial* found = nullptr;
        int   foundAxis = 1;
        float foundSign = 1.0f;
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == exclude) return;
            if (inst->IsA("Cube")) {
                Spatial* sp = static_cast<Spatial*>(inst);
                float bmin[3] = { sp->Position.x - sp->Size.x * 0.5f,
                                   sp->Position.y - sp->Size.y * 0.5f,
                                   sp->Position.z - sp->Size.z * 0.5f };
                float bmax[3] = { sp->Position.x + sp->Size.x * 0.5f,
                                   sp->Position.y + sp->Size.y * 0.5f,
                                   sp->Position.z + sp->Size.z * 0.5f };
                float rd[3] = { dir.x, dir.y, dir.z };
                float ro[3] = { ori.x, ori.y, ori.z };
                float tmin = -1e30f, tmax = 1e30f;
                bool  hit  = true;
                int   axis = 1;
                float sign = 1.0f;
                for (int i = 0; i < 3 && hit; ++i) {
                    if (std::abs(rd[i]) < 1e-8f) {
                        if (ro[i] < bmin[i] || ro[i] > bmax[i]) hit = false;
                    } else {
                        float t1 = (bmin[i] - ro[i]) / rd[i];
                        float t2 = (bmax[i] - ro[i]) / rd[i];
                        bool swapped = (t1 > t2);
                        if (swapped) std::swap(t1, t2);
                        if (t1 > tmin) {
                            tmin = t1;
                            axis = i;
                            // swapped = ray going - on this axis → entered from + face → sign +1
                            sign = swapped ? 1.0f : -1.0f;
                        }
                        tmax = (std::min)(tmax, t2);
                        if (tmax < tmin) hit = false;
                    }
                }
                if (hit && tmax >= 0.0f) {
                    float t = (tmin >= 0.0f) ? tmin : tmax;
                    if (t < nearestT) {
                        nearestT  = t;
                        found     = sp;
                        foundAxis = axis;
                        foundSign = sign;
                    }
                }
            }
            for (auto const& [_, child] : inst->getChildren()) self(self, child);
        };
        visit(visit, workspace);
        outAxis = foundAxis;
        outSign = foundSign;
        return found;
    };

    // AABB-AABB MTV 衝突フィット（moving と重なるキューブから押し出した位置を返す）
    auto fitCollision = [&](Vector3 pos, const Vector3& size, Instance* moving) -> Vector3 {
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == moving) return;
            if (inst->IsA("Spatial")) {
                Spatial* other = static_cast<Spatial*>(inst);
                float ox = (size.x + other->Size.x) * 0.5f - std::abs(pos.x - other->Position.x);
                float oy = (size.y + other->Size.y) * 0.5f - std::abs(pos.y - other->Position.y);
                float oz = (size.z + other->Size.z) * 0.5f - std::abs(pos.z - other->Position.z);
                if (ox > 0.0f && oy > 0.0f && oz > 0.0f) {
                    float dx = pos.x - other->Position.x;
                    float dy = pos.y - other->Position.y;
                    float dz = pos.z - other->Position.z;
                    if (ox <= oy && ox <= oz)
                        pos.x += (dx >= 0.0f ? ox : -ox);
                    else if (oy <= ox && oy <= oz)
                        pos.y += (dy >= 0.0f ? oy : -oy);
                    else
                        pos.z += (dz >= 0.0f ? oz : -oz);
                }
            }
            for (auto const& [_, child] : inst->getChildren()) self(self, child);
        };
        visit(visit, workspace);
        return pos;
    };

    // ---- Select モード: レイキャストでオブジェクト選択 ----
    if (selectOnly && isHoveringViewport && ImGui::IsMouseClicked(0) && user && workspace) {
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 winPos   = ImGui::GetWindowPos();
        Vector3 rayDir = makeRay(mousePos.x - winPos.x, mousePos.y - winPos.y);
        Vector3 rayOri = user->cpos;

        Instance* nearest = nullptr;
        float nearestT = 1e30f;

        auto castRay = [&](auto& self, Instance* inst) -> void {
            if (!inst) return;
            if (inst->IsA("Cube")) {
                Spatial* s = static_cast<Spatial*>(inst);
                float bmin[3] = { s->Position.x - s->Size.x * 0.5f,
                                   s->Position.y - s->Size.y * 0.5f,
                                   s->Position.z - s->Size.z * 0.5f };
                float bmax[3] = { s->Position.x + s->Size.x * 0.5f,
                                   s->Position.y + s->Size.y * 0.5f,
                                   s->Position.z + s->Size.z * 0.5f };
                float rd[3] = { rayDir.x, rayDir.y, rayDir.z };
                float ro[3] = { rayOri.x, rayOri.y, rayOri.z };
                float tmin = -1e30f, tmax = 1e30f;
                bool hit = true;
                for (int i = 0; i < 3 && hit; ++i) {
                    if (std::abs(rd[i]) < 1e-8f) {
                        if (ro[i] < bmin[i] || ro[i] > bmax[i]) hit = false;
                    } else {
                        float t1 = (bmin[i] - ro[i]) / rd[i];
                        float t2 = (bmax[i] - ro[i]) / rd[i];
                        if (t1 > t2) std::swap(t1, t2);
                        tmin = (std::max)(tmin, t1);
                        tmax = (std::min)(tmax, t2);
                        if (tmax < tmin) hit = false;
                    }
                }
                if (hit && tmax >= 0.0f) {
                    float t = (tmin >= 0.0f) ? tmin : tmax;
                    if (t < nearestT) { nearestT = t; nearest = inst; }
                }
            }
            for (auto const& [_, child] : inst->getChildren())
                self(self, child);
        };
        castRay(castRay, workspace);

        if (selectedInstance) *selectedInstance = nearest;
    }

    // フォーカス状態の可視化（フォーカス時に薄いボーダーを描画）
    if (isViewportFocused) {
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetWindowPos(),
            ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                   ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
            IM_COL32(0, 200, 255, 100),
            0.0f, 0, 2.0f
        );
    }

    // ---- ギズモのオーバーレイ ----
    if (!selectOnly && selectedInstance && *selectedInstance && user) {
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
                if (gizmoOp == ImGuizmo::TRANSLATE && workspace) {
                    // 軸ごとに独立してテストすることで、意図しない軸への飛び出しを防ぐ
                    Vector3 prev = s->Position;
                    Vector3 rx = fitCollision({newPos.x, prev.y,  prev.z},  s->Size, inst);
                    Vector3 ry = fitCollision({prev.x,  newPos.y, prev.z},  s->Size, inst);
                    Vector3 rz = fitCollision({prev.x,  prev.y,  newPos.z}, s->Size, inst);
                    newPos = Vector3(rx.x, ry.y, rz.z);
                }
                if (inst->IsA("BaseCube"))
                    static_cast<BaseCube*>(inst)->teleportTo(newPos);
                else
                    s->Position = newPos;
            }
        }
    }

    // ---- Move モード自由移動: 左ドラッグでサーフェスに追従 ----
    if (!selectOnly && gizmoOp == ImGuizmo::TRANSLATE
            && isViewportFocused && ImGui::IsMouseDown(0) && !ImGuizmo::IsUsing()
            && selectedInstance && *selectedInstance && user && workspace) {
        Instance* inst = *selectedInstance;
        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);
            ImVec2 mp = ImGui::GetMousePos();
            ImVec2 wp = ImGui::GetWindowPos();
            Vector3 dir = makeRay(mp.x - wp.x, mp.y - wp.y);
            Vector3 ori = user->cpos;

            [&]() {
                // レイキャストなしは移動しない
                int   hitAxis = 1;
                float hitSign = 1.0f;
                Spatial* surface = castRaySurface(ori, dir, inst, hitAxis, hitSign);
                if (!surface) return;

                // 衝突面の法線軸 (hitAxis) に沿ってオブジェクトを隣接配置し、
                // 残り2軸はレイと軸平面の交点で決定する
                float surfPos[3] = { surface->Position.x, surface->Position.y, surface->Position.z };
                float surfHalf[3] = { surface->Size.x * 0.5f, surface->Size.y * 0.5f, surface->Size.z * 0.5f };
                float movHalf[3]  = { s->Size.x * 0.5f, s->Size.y * 0.5f, s->Size.z * 0.5f };
                float oriArr[3]   = { ori.x, ori.y, ori.z };
                float dirArr[3]   = { dir.x, dir.y, dir.z };

                // 衝突面のワールド座標 → オブジェクト中心の固定座標
                float faceWorld  = surfPos[hitAxis] + hitSign * surfHalf[hitAxis];
                float fixedCoord = faceWorld + hitSign * movHalf[hitAxis];

                // レイと「fixedCoord に平行な軸平面」の交点
                if (std::abs(dirArr[hitAxis]) < 1e-6f) return; // レイが面に平行
                float t = (fixedCoord - oriArr[hitAxis]) / dirArr[hitAxis];
                if (t < 0.0f) return; // 平面がカメラ後方

                float newPosArr[3];
                for (int i = 0; i < 3; ++i) {
                    newPosArr[i] = (i == hitAxis) ? fixedCoord
                                                  : oriArr[i] + dirArr[i] * t;
                }
                Vector3 newPos(newPosArr[0], newPosArr[1], newPosArr[2]);

                newPos = fitCollision(newPos, s->Size, inst);
                if (inst->IsA("BaseCube"))
                    static_cast<BaseCube*>(inst)->teleportTo(newPos);
                else
                    s->Position = newPos;
            }();
        }
    }

    // ---- F キー: 選択オブジェクトにカメラをフォーカス ----
    if (isViewportFocused && user && selectedInstance && *selectedInstance
            && ImGui::IsKeyPressed(ImGuiKey_F)) {
        Instance* inst = *selectedInstance;
        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);
            Vector3 objPos = s->Position;
            float maxSize = (std::max)(s->Size.x, (std::max)(s->Size.y, s->Size.z));
            float dist = (std::max)(maxSize * 3.0f, 5.0f);
            user->cpos = objPos - user->forward * dist;
            user->updateVectors();
        }
    }

    ImGui::PopStyleVar();
    ImGui::End();
}
