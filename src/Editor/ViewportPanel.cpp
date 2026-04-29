#include <Editor/ViewportPanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/CommandHistory.hpp>
#include <Math/Matrix4.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Core/SystemState.hpp>
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

    // ワールド座標 → 対象 Spatial のローカル座標に変換
    auto worldToLocal = [](const Vector3& worldPos, const Spatial* sp) -> Vector3 {
        auto par = sp->Parent.lock();
        if (par && par->IsA("Spatial")) {
            CFrame pw = static_cast<Spatial*>(par.get())->getWorldCFrame();
            return pw.Rotation.conjugate().rotate(worldPos - pw.Position);
        }
        return worldPos;
    };

    // ワールド回転 → 対象 Spatial のローカル回転に変換
    auto worldToLocalRot = [](const Quaternion& worldRot, const Spatial* sp) -> Quaternion {
        auto par = sp->Parent.lock();
        if (par && par->IsA("Spatial")) {
            CFrame pw = static_cast<Spatial*>(par.get())->getWorldCFrame();
            return pw.Rotation.conjugate() * worldRot;
        }
        return worldRot;
    };

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
            if (inst->IsA("BaseCube")) {
                Spatial* sp = static_cast<Spatial*>(inst);
                Vector3 wp = sp->getWorldPosition();
                float bmin[3] = { wp.x - sp->Size.x * 0.5f,
                                   wp.y - sp->Size.y * 0.5f,
                                   wp.z - sp->Size.z * 0.5f };
                float bmax[3] = { wp.x + sp->Size.x * 0.5f,
                                   wp.y + sp->Size.y * 0.5f,
                                   wp.z + sp->Size.z * 0.5f };
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
            for (auto const& [_, child] : inst->getChildren()) self(self, child.get());
        };
        visit(visit, workspace);
        outAxis = foundAxis;
        outSign = foundSign;
        return found;
    };

    // 指定軸のみで衝突解決する（軸ジャンプ防止用）
    // axis: 0=X, 1=Y, 2=Z。その軸の解決後ワールド座標を返す
    auto fitOnAxis = [&](Vector3 pos, const Vector3& size, Instance* moving, int axis) -> float {
        float p[3]  = { pos.x,  pos.y,  pos.z  };
        float sz[3] = { size.x, size.y, size.z };
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == moving) return;
            if (inst->IsA("Spatial")) {
                Spatial* other = static_cast<Spatial*>(inst);
                Vector3 owp = other->getWorldPosition();
                float op[3] = { owp.x, owp.y, owp.z };
                float os[3] = { other->Size.x, other->Size.y, other->Size.z };
                float oa[3] = {
                    (sz[0] + os[0]) * 0.5f - std::abs(p[0] - op[0]),
                    (sz[1] + os[1]) * 0.5f - std::abs(p[1] - op[1]),
                    (sz[2] + os[2]) * 0.5f - std::abs(p[2] - op[2])
                };
                if (oa[0] > 0.0f && oa[1] > 0.0f && oa[2] > 0.0f) {
                    float d = p[axis] - op[axis];
                    p[axis] += (d >= 0.0f ? oa[axis] : -oa[axis]);
                }
            }
            for (auto const& [_, child] : inst->getChildren()) self(self, child.get());
        };
        visit(visit, workspace);
        return p[axis];
    };

    // AABB-AABB MTV 衝突フィット（moving と重なるキューブから押し出したワールド位置を返す）
    auto fitCollision = [&](Vector3 pos, const Vector3& size, Instance* moving) -> Vector3 {
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == moving) return;
            if (inst->IsA("Spatial")) {
                Spatial* other = static_cast<Spatial*>(inst);
                Vector3 owp = other->getWorldPosition();
                float ox = (size.x + other->Size.x) * 0.5f - std::abs(pos.x - owp.x);
                float oy = (size.y + other->Size.y) * 0.5f - std::abs(pos.y - owp.y);
                float oz = (size.z + other->Size.z) * 0.5f - std::abs(pos.z - owp.z);
                if (ox > 0.0f && oy > 0.0f && oz > 0.0f) {
                    float dx = pos.x - owp.x;
                    float dy = pos.y - owp.y;
                    float dz = pos.z - owp.z;
                    if (ox <= oy && ox <= oz)
                        pos.x += (dx >= 0.0f ? ox : -ox);
                    else if (oy <= ox && oy <= oz)
                        pos.y += (dy >= 0.0f ? oy : -oy);
                    else
                        pos.z += (dz >= 0.0f ? oz : -oz);
                }
            }
            for (auto const& [_, child] : inst->getChildren()) self(self, child.get());
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
            if (inst->IsA("BaseCube")) {
                Spatial* s = static_cast<Spatial*>(inst);
                Vector3 wp = s->getWorldPosition();
                float bmin[3] = { wp.x - s->Size.x * 0.5f,
                                   wp.y - s->Size.y * 0.5f,
                                   wp.z - s->Size.z * 0.5f };
                float bmax[3] = { wp.x + s->Size.x * 0.5f,
                                   wp.y + s->Size.y * 0.5f,
                                   wp.z + s->Size.z * 0.5f };
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
                self(self, child.get());
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

            // Gizmo Undo: ドラッグ開始/終了を検知して GizmoCommand を記録
            static bool       s_wasUsingGizmo = false;
            static GizmoState s_gizmoBefore;
            bool isUsingGizmo = ImGuizmo::IsUsing();

            if (!s_wasUsingGizmo && isUsingGizmo && inst->IsA("BaseCube")) {
                BaseCube* bc = static_cast<BaseCube*>(inst);
                s_gizmoBefore = { bc->Position, bc->Size, bc->Rotation };
            }
            if (s_wasUsingGizmo && !isUsingGizmo && inst->IsA("BaseCube") && m_history) {
                BaseCube* bc = static_cast<BaseCube*>(inst);
                GizmoState after = { bc->Position, bc->Size, bc->Rotation };
                auto bcSp = std::static_pointer_cast<BaseCube>(inst->shared_from_this());
                m_history->record(std::make_unique<GizmoCommand>(bcSp, s_gizmoBefore, after));
            }
            s_wasUsingGizmo = isUsingGizmo;

            float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
            Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
            Vector3 target = user->cpos + user->forward;
            Matrix4 view   = Matrix4::LookAt(user->cpos, target, user->up);

            Matrix4 model = s->getWorldCFrame().toMatrix4() *
                            Matrix4::Scale(s->Size.x, s->Size.y, s->Size.z);

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            ImVec2 winPos = ImGui::GetWindowPos();
            ImGuizmo::SetRect(winPos.x, winPos.y, (float)w, (float)h);

            float snapArr[3] = { snapTranslateVal, snapTranslateVal, snapTranslateVal };
            float rotSnap[3] = { snapRotateVal,    snapRotateVal,    snapRotateVal    };
            const float* snap = nullptr;
            if (gizmoOp == ImGuizmo::TRANSLATE && snapTranslate) snap = snapArr;
            else if (gizmoOp == ImGuizmo::ROTATE && snapRotate)  snap = rotSnap;

            if (ImGuizmo::Manipulate(view.m, proj.m, gizmoOp,
                                     ImGuizmo::WORLD, model.m, nullptr, snap)) {
                // モデル行列から TRS を分解
                float sx = std::sqrt(model.m[0]*model.m[0] + model.m[1]*model.m[1] + model.m[2]*model.m[2]);
                float sy = std::sqrt(model.m[4]*model.m[4] + model.m[5]*model.m[5] + model.m[6]*model.m[6]);
                float sz = std::sqrt(model.m[8]*model.m[8] + model.m[9]*model.m[9] + model.m[10]*model.m[10]);
                Vector3 newPos(model.m[12], model.m[13], model.m[14]);
                Vector3 newSize((std::max)(sx, 0.05f), (std::max)(sy, 0.05f), (std::max)(sz, 0.05f));

                // 正規化した回転行列からクォータニオンを抽出
                float rotM[16] = {0};
                if (sx > 1e-6f) { rotM[0]=model.m[0]/sx; rotM[1]=model.m[1]/sx; rotM[2]=model.m[2]/sx; }
                if (sy > 1e-6f) { rotM[4]=model.m[4]/sy; rotM[5]=model.m[5]/sy; rotM[6]=model.m[6]/sy; }
                if (sz > 1e-6f) { rotM[8]=model.m[8]/sz; rotM[9]=model.m[9]/sz; rotM[10]=model.m[10]/sz; }
                rotM[15] = 1.0f;
                Quaternion newRot = Quaternion::FromRotationMatrix(rotM);

                if (gizmoOp == ImGuizmo::TRANSLATE && workspace) {
                    // newPos はワールド座標
                    if (collisionFit) {
                        Vector3 prevWorld = s->getWorldPosition();
                        float rx = (std::abs(newPos.x - prevWorld.x) > 1e-5f)
                                   ? fitOnAxis({newPos.x, prevWorld.y, prevWorld.z}, s->Size, inst, 0)
                                   : prevWorld.x;
                        float ry = (std::abs(newPos.y - prevWorld.y) > 1e-5f)
                                   ? fitOnAxis({prevWorld.x, newPos.y, prevWorld.z}, s->Size, inst, 1)
                                   : prevWorld.y;
                        float rz = (std::abs(newPos.z - prevWorld.z) > 1e-5f)
                                   ? fitOnAxis({prevWorld.x, prevWorld.y, newPos.z}, s->Size, inst, 2)
                                   : prevWorld.z;
                        newPos = Vector3(rx, ry, rz);
                    }
                    // ワールド → ローカルに変換して設定
                    Vector3 localPos = worldToLocal(newPos, s);
                    if (inst->IsA("BaseCube"))
                        static_cast<BaseCube*>(inst)->teleportTo(localPos);
                    else
                        s->Position = localPos;
                } else if (gizmoOp == ImGuizmo::SCALE) {
                    if (inst->IsA("BaseCube")) {
                        static_cast<BaseCube*>(inst)->setSize(newSize);
                    } else {
                        s->Size = newSize;
                    }
                } else if (gizmoOp == ImGuizmo::ROTATE) {
                    // newRot はワールド回転 → ローカルに変換
                    Quaternion localRot = worldToLocalRot(newRot, s);
                    if (inst->IsA("BaseCube")) {
                        static_cast<BaseCube*>(inst)->setRotation(localRot);
                    } else {
                        s->cframe.Rotation = localRot;
                    }
                }
            }
        }
    }

    // ---- 自由移動ドラッグ開始/終了検出 ----
    static GizmoState s_freeDragBefore;
    bool wasDragging = m_isDraggingSelected;

    // ボタンを離したらリセット
    if (!ImGui::IsMouseDown(0)) m_isDraggingSelected = false;

    // ドラッグ終了時に GizmoCommand を記録
    if (wasDragging && !m_isDraggingSelected
            && selectedInstance && *selectedInstance
            && (*selectedInstance)->IsA("BaseCube") && m_history) {
        BaseCube* bc = static_cast<BaseCube*>(*selectedInstance);
        GizmoState after = { bc->Position, bc->Size, bc->Rotation };
        if (after.position.x != s_freeDragBefore.position.x ||
            after.position.y != s_freeDragBefore.position.y ||
            after.position.z != s_freeDragBefore.position.z) {
            auto bcSp = std::static_pointer_cast<BaseCube>((*selectedInstance)->shared_from_this());
            m_history->record(std::make_unique<GizmoCommand>(bcSp, s_freeDragBefore, after));
        }
    }

    // クリック開始フレームに選択キューブの AABB を判定（ギズモハンドル操作中は除外）
    if (ImGui::IsMouseClicked(0) && isHoveringViewport && !ImGuizmo::IsUsing()
            && selectedInstance && *selectedInstance && user) {
        Instance* clickInst = *selectedInstance;
        if (clickInst->IsA("Spatial")) {
            Spatial* sp = static_cast<Spatial*>(clickInst);
            ImVec2 mp = ImGui::GetMousePos(), wp = ImGui::GetWindowPos();
            Vector3 dir = makeRay(mp.x - wp.x, mp.y - wp.y);
            float rd[3] = { dir.x, dir.y, dir.z };
            float ro[3] = { user->cpos.x, user->cpos.y, user->cpos.z };
            Vector3 spWorld = sp->getWorldPosition();
            float bmin[3] = { spWorld.x - sp->Size.x*0.5f,
                               spWorld.y - sp->Size.y*0.5f,
                               spWorld.z - sp->Size.z*0.5f };
            float bmax[3] = { spWorld.x + sp->Size.x*0.5f,
                               spWorld.y + sp->Size.y*0.5f,
                               spWorld.z + sp->Size.z*0.5f };
            float tmin = -1e30f, tmax = 1e30f;
            bool cubeHit = true;
            for (int i = 0; i < 3 && cubeHit; ++i) {
                if (std::abs(rd[i]) < 1e-8f) {
                    if (ro[i] < bmin[i] || ro[i] > bmax[i]) cubeHit = false;
                } else {
                    float t1 = (bmin[i]-ro[i])/rd[i], t2 = (bmax[i]-ro[i])/rd[i];
                    if (t1 > t2) std::swap(t1, t2);
                    tmin = (std::max)(tmin, t1); tmax = (std::min)(tmax, t2);
                    if (tmax < tmin) cubeHit = false;
                }
            }
            m_isDraggingSelected = cubeHit && tmax >= 0.0f;
        }
    }

    // ドラッグ開始時に before をキャプチャ
    if (!wasDragging && m_isDraggingSelected
            && selectedInstance && *selectedInstance
            && (*selectedInstance)->IsA("BaseCube")) {
        BaseCube* bc = static_cast<BaseCube*>(*selectedInstance);
        s_freeDragBefore = { bc->Position, bc->Size, bc->Rotation };
    }

    // ---- Move モード自由移動: 選択キューブ上からのドラッグでサーフェスに追従 ----
    if (!selectOnly && gizmoOp == ImGuizmo::TRANSLATE
            && m_isDraggingSelected && !ImGuizmo::IsUsing()
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
                Vector3 surfWorld = surface->getWorldPosition();
                float surfPos[3] = { surfWorld.x, surfWorld.y, surfWorld.z };
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

                if (collisionFit) newPos = fitCollision(newPos, s->Size, inst);
                Vector3 localPos = worldToLocal(newPos, s);
                if (inst->IsA("BaseCube"))
                    static_cast<BaseCube*>(inst)->teleportTo(localPos);
                else
                    s->Position = localPos;
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
