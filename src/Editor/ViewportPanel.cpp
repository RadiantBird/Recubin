#include <Editor/ViewportPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
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

    // コンテンツ領域の画面座標原点（タイトルバー分を除いた正確な左上）
    ImVec2 contentOrigin;
    {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 cm = ImGui::GetWindowContentRegionMin();
        contentOrigin = ImVec2(wp.x + cm.x, wp.y + cm.y);
    }

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
            if (inst->GetClassName() == "Skybox") return;
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
            if (inst->GetClassName() == "Skybox") return;
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
            if (inst->GetClassName() == "Skybox") return;
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

    // ---- クリック処理: 選択 & ドラッグ開始（全モード共通） ----
    // Selectモードでも非Selectモードでもレイキャストでオブジェクトを選択できる。
    // 非Selectモードでは現在の選択物をクリックしたときのみドラッグ開始する。
    if (isHoveringViewport && ImGui::IsMouseClicked(0) && !ImGuizmo::IsUsing() && user && workspace) {
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector3 rayDir = makeRay(mousePos.x - contentOrigin.x, mousePos.y - contentOrigin.y);
        Vector3 rayOri = user->cpos;

        // OBB スラブ法: レイをオブジェクトのローカル空間に変換して AABB テストする
        // → 回転したオブジェクトでも正確に判定できる
        auto obbHit = [](const Vector3& ori, const Vector3& dir,
                         const CFrame& worldCF, const Vector3& size) -> float {
            // レイをオブジェクトローカル空間へ変換
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

        // ---- ピッカーモード: Pick ボタン押下中はクリックをキューブ指定に横取り ----
        if (m_picker && m_picker->active) {
            Instance* nearest = nullptr;
            float nearestT = 1e30f;
            auto pickerCast = [&](auto& self, Instance* node) -> void {
                if (!node) return;
                if (node->IsA("BaseCube")) {
                    auto* s = static_cast<Spatial*>(node);
                    float t = obbHit(rayOri, rayDir, s->getWorldCFrame(), s->Size);
                    if (t >= 0.0f && t < nearestT) { nearestT = t; nearest = node; }
                }
                for (auto& [_, c] : node->getChildren()) self(self, c.get());
            };
            pickerCast(pickerCast, workspace);
            if (nearest && nearest != m_picker->constraint)
                m_picker->onPick(std::static_pointer_cast<BaseCube>(nearest->shared_from_this()));
            m_picker->active     = false;
            m_isDraggingSelected = false;
            m_isBoxSelectArmed   = false;
        } else {
        // Step 1: 現在の選択物のOBBにヒットしたか判定（非Selectモードのドラッグ用）
        bool hitSelected = false;
        if (!selectOnly && selectedInstance && *selectedInstance && (*selectedInstance)->IsA("Spatial")) {
            Spatial* sp = static_cast<Spatial*>(*selectedInstance);
            float t = obbHit(rayOri, rayDir, sp->getWorldCFrame(), sp->Size);
            hitSelected = (t >= 0.0f);
        }
        m_isDraggingSelected = hitSelected;

        // Step 2: Selectモード、または非SelectモードでShift+クリックかつ選択物にヒットしなかった場合
        //         → レイキャストで選択変更
        bool shiftHeld = ImGui::GetIO().KeyShift;
        bool clickFoundSomething = false;
        if ((selectOnly || (shiftHeld && !hitSelected)) && selectedInstance) {
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
            castRay(castRay, workspace);
            *selectedInstance = nearest;
            if (selectedInstances) {
                selectedInstances->clear();
                if (nearest) selectedInstances->push_back(nearest);
            }
            clickFoundSomething = (nearest != nullptr);
        }

        // ボックス選択 arm: Selectモードのみ、何にもヒットしなかった場合にドラッグで開始
        // 非Selectモードではギズモ操作と競合するため arm しない
        m_isBoxSelectArmed = isSelectMode() && !clickFoundSomething && !shiftHeld;
        m_boxSelectStart   = ImGui::GetMousePos();
        } // end else (not picking)
    }

    // ---- ボックス選択 ----
    // ドラッグ閾値(5px²)を超えたら選択モード開始（Selectモード専用）
    if (m_isBoxSelectArmed && isSelectMode() && ImGui::IsMouseDown(0) && isHoveringViewport) {
        ImVec2 cur = ImGui::GetMousePos();
        float dx = cur.x - m_boxSelectStart.x, dy = cur.y - m_boxSelectStart.y;
        if (dx*dx + dy*dy > 25.0f) m_isBoxSelecting = true;
    }

    // ボックスオーバーレイ描画
    if (m_isBoxSelecting) {
        ImVec2 cur = ImGui::GetMousePos();
        ImVec2 a = { (std::min)(m_boxSelectStart.x, cur.x), (std::min)(m_boxSelectStart.y, cur.y) };
        ImVec2 b = { (std::max)(m_boxSelectStart.x, cur.x), (std::max)(m_boxSelectStart.y, cur.y) };
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, IM_COL32(100, 160, 255, 40));
        dl->AddRect      (a, b, IM_COL32(100, 160, 255, 200), 0.0f, 0, 1.5f);
    }

    // マウスを離したとき: ボックス内のオブジェクトを選択
    if (!ImGui::IsMouseDown(0) && (m_isBoxSelecting || m_isBoxSelectArmed)) {
        if (m_isBoxSelecting && user && workspace && selectedInstance && selectedInstances) {
            float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
            Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
            Vector3 camTarget = user->cpos + user->forward;
            Matrix4 view = Matrix4::LookAt(user->cpos, camTarget, user->up);
            Matrix4 vp   = proj * view;
            const float* mv = vp.m;

            ImVec2 cur = ImGui::GetMousePos();
            float minX = (std::min)(m_boxSelectStart.x, cur.x);
            float maxX = (std::max)(m_boxSelectStart.x, cur.x);
            float minY = (std::min)(m_boxSelectStart.y, cur.y);
            float maxY = (std::max)(m_boxSelectStart.y, cur.y);

            selectedInstances->clear();
            *selectedInstance = nullptr;

            auto collect = [&](auto& self, Instance* node) -> void {
                if (!node || node->GetClassName() == "Skybox") return;
                if (node->IsA("BaseCube")) {
                    Spatial* sp = static_cast<Spatial*>(node);
                    Vector3 wp  = sp->getWorldPosition();
                    // column-major VP 行列でワールド座標をスクリーン投影
                    float cx = mv[0]*wp.x + mv[4]*wp.y + mv[8]*wp.z  + mv[12];
                    float cy = mv[1]*wp.x + mv[5]*wp.y + mv[9]*wp.z  + mv[13];
                    float cw = mv[3]*wp.x + mv[7]*wp.y + mv[11]*wp.z + mv[15];
                    if (cw > 0.0f) {
                        float sx = contentOrigin.x + (cx/cw + 1.0f) * 0.5f * (float)w;
                        float sy = contentOrigin.y + (1.0f - cy/cw) * 0.5f * (float)h;
                        if (sx >= minX && sx <= maxX && sy >= minY && sy <= maxY)
                            selectedInstances->push_back(node);
                    }
                }
                for (auto const& [_, ch] : node->getChildren()) self(self, ch.get());
            };
            collect(collect, workspace);

            if (!selectedInstances->empty())
                *selectedInstance = selectedInstances->front();
        }
        m_isBoxSelecting   = false;
        m_isBoxSelectArmed = false;
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
    if (!isSelectMode() && selectedInstance && *selectedInstance && user) {
        Instance* inst = *selectedInstance;
        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);

            // Gizmo Undo: ドラッグ開始/終了を検知して MultiGizmoCommand を記録
            bool isUsingGizmo = ImGuizmo::IsUsing();

            if (!m_wasUsingGizmo && isUsingGizmo) {
                m_gizmoEntries.clear();
                // 複数選択中は全対象をキャプチャ、単一なら primary のみ
                std::vector<Instance*> targets = hasMultiSelection()
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
            Vector3 target = user->cpos + user->forward;
            Matrix4 view   = Matrix4::LookAt(user->cpos, target, user->up);

            // Roblox スタイルリサイズ用: ドラッグ中でない間は常に最新状態を保持
            // IsUsing() は Manipulate() 呼び出し前の状態を返すため、
            // ドラッグ開始の最初フレームでは !isUsingGizmo が true になり正確な初期値を確保できる
            if (!isUsingGizmo && gizmoOp == ImGuizmo::SCALE) {
                m_scaleBeforeSize     = s->Size;
                m_scaleBeforeWorldPos = s->getWorldPosition();
            }

            // SCALE ドラッグ中は開始時の不変行列を ImGuizmo に渡す
            // 毎フレーム変化する行列を渡すと ImGuizmo の内部参照がずれて特異点が生まれるため
            Matrix4 model;
            if (isUsingGizmo && gizmoOp == ImGuizmo::SCALE) {
                CFrame stableCF = s->getWorldCFrame();
                stableCF.Position = m_scaleBeforeWorldPos;
                model = stableCF.toMatrix4() *
                        Matrix4::Scale(m_scaleBeforeSize.x, m_scaleBeforeSize.y, m_scaleBeforeSize.z);
            } else {
                model = s->getWorldCFrame().toMatrix4() *
                        Matrix4::Scale(s->Size.x, s->Size.y, s->Size.z);
            }

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            ImGuizmo::SetRect(contentOrigin.x, contentOrigin.y, (float)w, (float)h);

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
                    // teleportTo 前のワールド座標を保存（複数選択の delta 計算用）
                    Vector3 prevPrimaryWorld = s->getWorldPosition();

                    // newPos はワールド座標
                    if (collisionFit) {
                        Vector3 prevWorld = prevPrimaryWorld;
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

                    // 複数選択: primary の delta を残りのオブジェクトに適用
                    if (hasMultiSelection()) {
                        Vector3 deltaWorld = s->getWorldPosition() - prevPrimaryWorld;
                        for (Instance* other : *selectedInstances) {
                            if (!other || other->Parent.expired() || other == inst || !other->IsA("BaseCube")) continue;
                            BaseCube* bc = static_cast<BaseCube*>(other);
                            Vector3 nw = bc->getWorldPosition() + deltaWorld;
                            bc->teleportTo(worldToLocal(nw, bc));
                        }
                    }
                } else if (gizmoOp == ImGuizmo::SCALE) {
                    // Roblox スタイル: size デルタの半分だけ position をオフセット
                    // ドラッグ開始時の絶対座標から計算することで毎フレームの累積を防ぐ
                    Vector3 deltaSize = newSize - m_scaleBeforeSize;
                    Vector3 newWorldPos = m_scaleBeforeWorldPos + deltaSize * 0.5f;
                    Vector3 localPos = worldToLocal(newWorldPos, s);
                    if (inst->IsA("BaseCube")) {
                        BaseCube* bc = static_cast<BaseCube*>(inst);
                        bc->teleportTo(localPos);
                        bc->setSize(newSize);
                    } else {
                        s->Position = localPos;
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

    // ---- 複数選択ハイライト: 全選択オブジェクトの OBB を画面投影してアウトラインを描画 ----
    if (user && selectedInstances && !selectedInstances->empty()) {
        float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
        Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
        Matrix4 view = Matrix4::LookAt(user->cpos, user->cpos + user->forward, user->up);
        Matrix4 vp   = proj * view;
        const float* mv = vp.m;
        auto* dl = ImGui::GetWindowDrawList();

        for (Instance* inst : *selectedInstances) {
            if (!inst || inst->Parent.expired() || !inst->IsA("BaseCube")) continue;
            Spatial* sp = static_cast<Spatial*>(inst);
            CFrame   wf = sp->getWorldCFrame();
            float hx = sp->Size.x * 0.5f, hy = sp->Size.y * 0.5f, hz = sp->Size.z * 0.5f;

            // OBB 8頂点を画面投影し、スクリーン AABB を求める
            float sxMin = 1e30f, sxMax = -1e30f;
            float syMin = 1e30f, syMax = -1e30f;
            bool anyVis = false;
            for (int ci = 0; ci < 8; ++ci) {
                float lx = (ci & 1) ? hx : -hx;
                float ly = (ci & 2) ? hy : -hy;
                float lz = (ci & 4) ? hz : -hz;
                Vector3 wc = wf.Position + wf.Rotation.rotate(Vector3(lx, ly, lz));
                float cx = mv[0]*wc.x + mv[4]*wc.y + mv[8]*wc.z  + mv[12];
                float cy = mv[1]*wc.x + mv[5]*wc.y + mv[9]*wc.z  + mv[13];
                float cw = mv[3]*wc.x + mv[7]*wc.y + mv[11]*wc.z + mv[15];
                if (cw <= 0.0f) continue;
                float sx = contentOrigin.x + (cx/cw + 1.0f) * 0.5f * (float)w;
                float sy = contentOrigin.y + (1.0f - cy/cw) * 0.5f * (float)h;
                sxMin = (std::min)(sxMin, sx); sxMax = (std::max)(sxMax, sx);
                syMin = (std::min)(syMin, sy); syMax = (std::max)(syMax, sy);
                anyVis = true;
            }
            if (!anyVis) continue;

            bool isPrimary = (selectedInstance && *selectedInstance == inst);
            ImU32 col = isPrimary
                ? IM_COL32(255, 240,  80, 220)   // 黄: primary
                : IM_COL32(255, 150,  30, 180);  // 橙: secondary
            dl->AddRect(ImVec2(sxMin - 2.0f, syMin - 2.0f),
                        ImVec2(sxMax + 2.0f, syMax + 2.0f), col, 0.0f, 0, 2.0f);
        }
    }

    // ---- 自由移動ドラッグ開始/終了検出 ----
    bool wasDragging = m_isDraggingSelected;

    // ボタンを離したらリセット
    if (!ImGui::IsMouseDown(0)) m_isDraggingSelected = false;

    // ドラッグ終了時に MultiGizmoCommand を記録
    if (wasDragging && !m_isDraggingSelected && m_history && !m_freeDragEntries.empty()) {
        bool anyChanged = false;
        for (auto& e : m_freeDragEntries) {
            if (e.target && !e.target->Parent.expired()) {
                e.after = { e.target->Position, e.target->Size, e.target->Rotation };
                if (e.after.position.x != e.before.position.x ||
                    e.after.position.y != e.before.position.y ||
                    e.after.position.z != e.before.position.z)
                    anyChanged = true;
            }
        }
        if (anyChanged)
            m_history->record(std::make_unique<MultiGizmoCommand>(std::move(m_freeDragEntries)));
        m_freeDragEntries.clear();
    }

    // ドラッグ開始時に全選択対象の before をキャプチャ
    if (!wasDragging && m_isDraggingSelected && selectedInstance && *selectedInstance) {
        m_freeDragEntries.clear();
        std::vector<Instance*> targets = hasMultiSelection()
            ? *selectedInstances : std::vector<Instance*>{ *selectedInstance };
        for (Instance* tgt : targets) {
            if (tgt && !tgt->Parent.expired() && tgt->IsA("BaseCube")) {
                BaseCube* bc = static_cast<BaseCube*>(tgt);
                m_freeDragEntries.push_back({
                    std::static_pointer_cast<BaseCube>(tgt->shared_from_this()),
                    { bc->Position, bc->Size, bc->Rotation }, {}
                });
            }
        }
    }

    // ---- Move モード自由移動: 選択キューブ上からのドラッグでサーフェスに追従 ----
    if (isMoveMode() && m_isDraggingSelected && !ImGuizmo::IsUsing()
            && selectedInstance && *selectedInstance && user && workspace) {
        Instance* inst = *selectedInstance;
        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);
            ImVec2 mp = ImGui::GetMousePos();
            Vector3 dir = makeRay(mp.x - contentOrigin.x, mp.y - contentOrigin.y);
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
                Vector3 prevPrimaryWorld = s->getWorldPosition();
                Vector3 localPos = worldToLocal(newPos, s);
                if (inst->IsA("BaseCube"))
                    static_cast<BaseCube*>(inst)->teleportTo(localPos);
                else
                    s->Position = localPos;

                // 複数選択: primary の delta を残りのオブジェクトに適用
                if (hasMultiSelection()) {
                    Vector3 deltaWorld = s->getWorldPosition() - prevPrimaryWorld;
                    for (Instance* other : *selectedInstances) {
                        if (!other || other->Parent.expired() || other == inst || !other->IsA("BaseCube")) continue;
                        BaseCube* bc = static_cast<BaseCube*>(other);
                        Vector3 nw = bc->getWorldPosition() + deltaWorld;
                        bc->teleportTo(worldToLocal(nw, bc));
                    }
                }
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
