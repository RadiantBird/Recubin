#include <Editor/ViewportPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/CommandHistory.hpp>
#include <Math/Matrix4.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/System.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/Decal.hpp>
#include <Core/SystemState.hpp>
#include <Core/Renderer.hpp>
#include <Core/Physics.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
#include <algorithm>
#include <cmath>
#include <functional>

// NOTE: ImGuizmoは乗算でスケール処理を行っている

// 対象SpatialのOBB8頂点をワールド空間で求め、AABBに集約する（複数選択中心・ModelのAABB計算で共用）
static void accumulateWorldAABB(Spatial* sp, Vector3& mn, Vector3& mx) {
    CFrame wf = sp->getWorldCFrame();
    float hx = sp->Size.x * 0.5f, hy = sp->Size.y * 0.5f, hz = sp->Size.z * 0.5f;
    for (int ci = 0; ci < 8; ++ci) {
        float lx = (ci & 1) ? hx : -hx;
        float ly = (ci & 2) ? hy : -hy;
        float lz = (ci & 4) ? hz : -hz;
        Vector3 wc = wf.Position + wf.Rotation.rotate(Vector3(lx, ly, lz));
        mn.x = (std::min)(mn.x, wc.x); mx.x = (std::max)(mx.x, wc.x);
        mn.y = (std::min)(mn.y, wc.y); mx.y = (std::max)(mx.y, wc.y);
        mn.z = (std::min)(mn.z, wc.z); mx.z = (std::max)(mx.z, wc.z);
    }
}

// rootの子孫を再帰し、BaseCubeのワールドOBBをAABBに集約する。1つも見つからなければfalse
static bool computeDescendantWorldAABB(Instance* root, Vector3& outMin, Vector3& outMax) {
    bool found = false;
    Vector3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    std::function<void(Instance*)> visit = [&](Instance* inst) {
        if (!inst) return;
        if (inst->IsA("BaseCube")) {
            accumulateWorldAABB(static_cast<Spatial*>(inst), mn, mx);
            found = true;
        }
        for (auto const& [_, child] : inst->getChildren()) visit(child.get());
    };
    for (auto const& [_, child] : root->getChildren()) visit(child.get());
    if (found) { outMin = mn; outMax = mx; }
    return found;
}

// ===================================================
//  ViewportPanel 実装
// ===================================================

ViewportPanel::ViewportPanel()
    : EditorPanel("Viewport") {
    initFBO(fbWidth, fbHeight);
}

ViewportPanel::~ViewportPanel() {
    if (m_ownCamDragging && user) user->endExternalCameraDrag();
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
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    // printf("\033[36m[FBO CHECK] fb=%u, tex=%u, status=0x%X (Complete=0x8CD5)\033[0m\n", framebuffer, colorTexture, status);
}

// 独立カメラ: yaw/pitchから回転を合成（yaw→pitch、-Z前方）
Quaternion ViewportPanel::ownCamRot() const {
    Quaternion qYaw   = Quaternion::fromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), m_camYaw);
    Quaternion qPitch = Quaternion::fromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), m_camPitch);
    return qYaw * qPitch;
}

Vector3 ViewportPanel::camPos() const     { return m_useOwnCamera ? m_camPos : user->cpos; }
Vector3 ViewportPanel::camForward() const { return m_useOwnCamera ? ownCamRot().getForward() : user->forward; }
Vector3 ViewportPanel::camRight() const   { return m_useOwnCamera ? ownCamRot().getRight()   : user->right; }
Vector3 ViewportPanel::camUp() const      { return m_useOwnCamera ? ownCamRot().getUp()      : user->up; }

// userカメラの位置/向きから独立カメラを初期化（yaw/pitchをforwardから復元）
void ViewportPanel::initOwnCameraFrom(const User& u) {
    m_camPos = u.cpos;
    Vector3 f = u.forward;
    float fy = std::clamp(f.y, -1.0f, 1.0f);
    m_camPitch = std::asin(fy) * 180.0f / 3.14159265f;
    m_camYaw   = std::atan2(-f.x, -f.z) * 180.0f / 3.14159265f;
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
    // printf("\033[36m[CHECK] workspace=%p, user=%p, hovered=%d\033[0m\n", workspace, user, isHoveringViewport);
    // パディングを削除してゲームビューをパネルに密着させる
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title.c_str(), &isOpen,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    // ImGuizmoのグローバル状態(gContext)をビューポート毎に分離する
    // (複数ビューポートが同一フレームでManipulate/IsUsingを呼んでも干渉しないように)
    ImGuizmo::PushID(this);

    // パネルの利用可能サイズ
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f) avail.x = 1.0f;
    if (avail.y < 1.0f) avail.y = 1.0f;

    // System.BaseResolution のアスペクト比を維持したレターボックスサイズを計算
    // (System が見つからない/不正な場合は既定 1920x1080 のアスペクトにフォールバック)
    float baseW = 1920.f, baseH = 1080.f;
    if (workspace) {
        if (auto parent = workspace->Parent.lock(); parent && parent->IsA("System")) {
            auto* sys = static_cast<System*>(parent.get());
            if (sys->BaseResolution.x > 0.f) baseW = sys->BaseResolution.x;
            if (sys->BaseResolution.y > 0.f) baseH = sys->BaseResolution.y;
        }
    }
    float letterboxScale = (std::min)(avail.x / baseW, avail.y / baseH);
    int w = (int)(baseW * letterboxScale);
    int h = (int)(baseH * letterboxScale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    resizeFBO(w, h);

    if (workspace && user && Renderer::instance) {
        ViewportRenderDesc desc;
        desc.workspace         = workspace;          // 有効なワークスペース
        desc.fbo               = framebuffer;       // ログでCompleteだったこのパネルのFBOテクスチャ
        desc.width             = w;
        desc.height            = h;
        desc.cameraPosition    = camPos();           // 有効なユーザーカメラ位置
        desc.cameraForward     = camForward();
        desc.cameraUp          = camUp();
        desc.renderShadows     = true;
        desc.renderConstraints = true;
        desc.renderHighlights  = true;              // サブ側は一旦ハイライトなし
        desc.renderPhysicsDebug = showPhysicsDebug;
        desc.isFocused         = isViewportFocused;

        // レンダラーにこのサブテクスチャへ描き込ませる！
        Renderer::instance->renderViewport(desc);
    }

    // パネルの画面座標原点（タイトルバー分を除いた正確な左上）
    ImVec2 panelOrigin;
    {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 cm = ImGui::GetWindowContentRegionMin();
        panelOrigin = ImVec2(wp.x + cm.x, wp.y + cm.y);
    }

    // レターボックス（黒帯）: パネル全体を黒で塗りつぶしてから中央にFBO画像を配置する
    ImGui::GetWindowDrawList()->AddRectFilled(
        panelOrigin, ImVec2(panelOrigin.x + avail.x, panelOrigin.y + avail.y), IM_COL32(0, 0, 0, 255));

    // FBO画像の実際の画面原点（レターボックス分オフセット）。以降のレイキャスト・ギズモも
    // この contentOrigin と w/h（レターボックス後のFBOサイズ）を使うため自動的に追従する
    ImVec2 contentOrigin(
        panelOrigin.x + (avail.x - (float)w) * 0.5f,
        panelOrigin.y + (avail.y - (float)h) * 0.5f);

    // FBO のカラーテクスチャを表示
    // ImTextureRef で GLuint を包む（v1.92 以降の API）
    ImGui::SetCursorScreenPos(contentOrigin);
    ImTextureRef texRef((ImTextureID)(uintptr_t)colorTexture);
    ImGui::Image(texRef, ImVec2((float)w, (float)h), ImVec2(0, 1), ImVec2(1, 0)); // Y 反転

    // ゲーム内 GUI をビューポート上に重ねて描画
    if (workspace && Renderer::instance) {
        Renderer::instance->renderGameGui(*workspace, user, contentOrigin.x, contentOrigin.y, (float)w, (float)h);
    }

    // レターボックス外側の黒帯を最前面に重ね描きする（BillboardGui等の3D投影GUIは
    // レターボックス矩形外へはみ出して描画されうるため、最後に上書きして必ず隠す）
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float panelL = panelOrigin.x,          panelT = panelOrigin.y;
        float panelR = panelOrigin.x + avail.x, panelB = panelOrigin.y + avail.y;
        float imgL = contentOrigin.x,           imgT = contentOrigin.y;
        float imgR = contentOrigin.x + (float)w, imgB = contentOrigin.y + (float)h;
        if (imgL > panelL) dl->AddRectFilled(ImVec2(panelL, panelT), ImVec2(imgL, panelB), IM_COL32(0, 0, 0, 255));
        if (imgR < panelR) dl->AddRectFilled(ImVec2(imgR, panelT), ImVec2(panelR, panelB), IM_COL32(0, 0, 0, 255));
        if (imgT > panelT) dl->AddRectFilled(ImVec2(panelL, panelT), ImVec2(panelR, imgT), IM_COL32(0, 0, 0, 255));
        if (imgB < panelB) dl->AddRectFilled(ImVec2(panelL, imgB), ImVec2(panelR, panelB), IM_COL32(0, 0, 0, 255));
    }

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
    //  独立カメラの入力処理（セカンダリビューポートのみ）
    // ===================================================
    if (m_useOwnCamera) {
        ImGuiIO& io = ImGui::GetIO();
        // 右ドラッグで視点回転（プライマリと同じカーソルロック+アンカー方式）
        bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (!m_ownCamDragging) {
            if (isHoveringViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                user->beginExternalCameraDrag();
                m_ownCamDragging = true;
            }
        } else if (rightDown) {
            double dx = 0.0, dy = 0.0;
            user->sampleExternalCameraDrag(dx, dy);
            m_camYaw   -= static_cast<float>(dx) * user->mouseRotationSpeed;
            m_camPitch -= static_cast<float>(dy) * user->mouseRotationSpeed;
            if (m_camPitch >  89.0f) m_camPitch =  89.0f;
            if (m_camPitch < -89.0f) m_camPitch = -89.0f;
        } else {
            user->endExternalCameraDrag();
            m_ownCamDragging = false;
        }
        // フォーカス中はWASD+E/Qで移動
        if (isViewportFocused && !io.WantTextInput) {
            float spd = user->speed;
            Vector3 fwd = camForward(), right = camRight(), up = camUp();
            if (ImGui::IsKeyDown(ImGuiKey_W)) m_camPos = m_camPos + fwd   *  spd;
            if (ImGui::IsKeyDown(ImGuiKey_S)) m_camPos = m_camPos + fwd   * -spd;
            if (ImGui::IsKeyDown(ImGuiKey_D)) m_camPos = m_camPos + right *  spd;
            if (ImGui::IsKeyDown(ImGuiKey_A)) m_camPos = m_camPos + right * -spd;
            if (ImGui::IsKeyDown(ImGuiKey_E)) m_camPos = m_camPos + up    *  spd;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) m_camPos = m_camPos + up    * -spd;
        }
        // ホバー中ホイールで前後ドリー
        if (isHoveringViewport && io.MouseWheel != 0.0f) {
            m_camPos = m_camPos + camForward() * (io.MouseWheel * user->mouseZoomSpeed);
        }
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
        return (camForward()
              + camRight() * (ndcX * aspect * tanH)
              + camUp()    * (ndcY * tanH)).normalize();
    };

    // OBB スラブ法: レイをオブジェクトのローカル空間に変換して AABB テストする
    // → 回転したオブジェクトでも正確に判定できる（クリック選択・Tabピボットで共用）
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

    // OBBを任意のワールド方向dirへ投影した半径(dirは正規化済み前提)
    auto obbSupportRadius = [](const Quaternion& rot, const Vector3& size, const Vector3& dir) -> float {
        Vector3 ex = rot.rotate(Vector3(1.0f, 0.0f, 0.0f));
        Vector3 ey = rot.rotate(Vector3(0.0f, 1.0f, 0.0f));
        Vector3 ez = rot.rotate(Vector3(0.0f, 0.0f, 1.0f));
        return std::abs(Vector3::Dot(dir, ex)) * size.x * 0.5f
             + std::abs(Vector3::Dot(dir, ey)) * size.y * 0.5f
             + std::abs(Vector3::Dot(dir, ez)) * size.z * 0.5f;
    };

    // OBB同士のSAT交差判定。15分離軸(Aの3軸 + Bの3軸 + 外積9軸)。
    auto obbIntersects = [&](const Vector3& posA, const Quaternion& rotA, const Vector3& sizeA,
                             const Vector3& posB, const Quaternion& rotB, const Vector3& sizeB) -> bool {
        Vector3 axesA[3] = {
            rotA.rotate(Vector3(1.0f, 0.0f, 0.0f)),
            rotA.rotate(Vector3(0.0f, 1.0f, 0.0f)),
            rotA.rotate(Vector3(0.0f, 0.0f, 1.0f))
        };
        Vector3 axesB[3] = {
            rotB.rotate(Vector3(1.0f, 0.0f, 0.0f)),
            rotB.rotate(Vector3(0.0f, 1.0f, 0.0f)),
            rotB.rotate(Vector3(0.0f, 0.0f, 1.0f))
        };
        Vector3 centerDiff = posB - posA;

        // Aの3軸 + Bの3軸
        for (int i = 0; i < 3; ++i) {
            if (std::abs(Vector3::Dot(axesA[i], centerDiff)) >=
                obbSupportRadius(rotA, sizeA, axesA[i]) + obbSupportRadius(rotB, sizeB, axesA[i]))
                return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (std::abs(Vector3::Dot(axesB[i], centerDiff)) >=
                obbSupportRadius(rotA, sizeA, axesB[i]) + obbSupportRadius(rotB, sizeB, axesB[i]))
                return false;
        }

        // 外積9軸
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                Vector3 axis = Vector3::Cross(axesA[i], axesB[j]);
                float lenSq = Vector3::Dot(axis, axis);
                if (lenSq < 1e-6f) continue; // 平行軸ペアはスキップ
                axis = axis.normalize();
                if (std::abs(Vector3::Dot(axis, centerDiff)) >=
                    obbSupportRadius(rotA, sizeA, axis) + obbSupportRadius(rotB, sizeB, axis))
                    return false;
            }
        }
        return true;
    };

    // OBB スラブ法レイキャスト
    // 最近傍の Spatial* を返し、衝突軸(0-2)と法線符号(+1/-1)を出力する
    // → 出力軸/符号はサーフェスのローカル軸インデックスと符号
    auto castRaySurface = [&](const Vector3& ori, const Vector3& dir,
                              Instance* exclude,
                              int& outAxis, float& outSign) -> Spatial* {
        float nearestT = 1e30f;
        Spatial* found = nullptr;
        int   foundAxis = 1;
        float foundSign = 1.0f;
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == exclude) return;
            if (inst->getClassName() == "Skybox") return;
            if (inst->IsA("BaseCube")) {
                Spatial* sp = static_cast<Spatial*>(inst);
                CFrame wf = sp->getWorldCFrame();
                // レイをサーフェスのローカル空間へ変換してAABBスラブ法で判定する
                Quaternion invRot = wf.Rotation.conjugate();
                Vector3 lo = invRot.rotate(ori - wf.Position);
                Vector3 ld = invRot.rotate(dir);
                float bmin[3] = { -sp->Size.x * 0.5f, -sp->Size.y * 0.5f, -sp->Size.z * 0.5f };
                float bmax[3] = {  sp->Size.x * 0.5f,  sp->Size.y * 0.5f,  sp->Size.z * 0.5f };
                float rd[3] = { ld.x, ld.y, ld.z };
                float ro[3] = { lo.x, lo.y, lo.z };
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
        Quaternion movRot = static_cast<Spatial*>(moving)->getWorldCFrame().Rotation;
        Vector3 eAxis = (axis == 0) ? Vector3(1.0f, 0.0f, 0.0f)
                      : (axis == 1) ? Vector3(0.0f, 1.0f, 0.0f)
                                    : Vector3(0.0f, 0.0f, 1.0f);
        float p[3] = { pos.x, pos.y, pos.z };
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == moving) return;
            if (inst->getClassName() == "Skybox") return;
            if (inst->IsA("Spatial")) {
                Spatial* other = static_cast<Spatial*>(inst);
                Vector3 owp = other->getWorldPosition();
                Quaternion otherRot = other->getWorldCFrame().Rotation;
                float op[3] = { owp.x, owp.y, owp.z };
                if (obbIntersects(Vector3(p[0], p[1], p[2]), movRot, size, owp, otherRot, other->Size)) {
                    float oa = obbSupportRadius(movRot, size, eAxis)
                             + obbSupportRadius(otherRot, other->Size, eAxis)
                             - std::abs(p[axis] - op[axis]);
                    if (oa > 0.0f) {
                        float d = p[axis] - op[axis];
                        p[axis] += (d >= 0.0f ? oa : -oa);
                    }
                }
            }
            for (auto const& [_, child] : inst->getChildren()) self(self, child.get());
        };
        visit(visit, workspace);
        return p[axis];
    };

    // OBB-OBB MTV 衝突フィット（moving と重なるキューブから押し出したワールド位置を返す）
    auto fitCollision = [&](Vector3 pos, const Vector3& size, Instance* moving) -> Vector3 {
        Quaternion movRot = static_cast<Spatial*>(moving)->getWorldCFrame().Rotation;
        auto visit = [&](auto& self, Instance* inst) -> void {
            if (!inst || inst == moving) return;
            if (inst->getClassName() == "Skybox") return;
            if (inst->IsA("Spatial")) {
                Spatial* other = static_cast<Spatial*>(inst);
                Vector3 owp = other->getWorldPosition();
                Quaternion otherRot = other->getWorldCFrame().Rotation;
                if (obbIntersects(pos, movRot, size, owp, otherRot, other->Size)) {
                    float ox = obbSupportRadius(movRot, size, Vector3(1.0f, 0.0f, 0.0f))
                             + obbSupportRadius(otherRot, other->Size, Vector3(1.0f, 0.0f, 0.0f))
                             - std::abs(pos.x - owp.x);
                    float oy = obbSupportRadius(movRot, size, Vector3(0.0f, 1.0f, 0.0f))
                             + obbSupportRadius(otherRot, other->Size, Vector3(0.0f, 1.0f, 0.0f))
                             - std::abs(pos.y - owp.y);
                    float oz = obbSupportRadius(movRot, size, Vector3(0.0f, 0.0f, 1.0f))
                             + obbSupportRadius(otherRot, other->Size, Vector3(0.0f, 0.0f, 1.0f))
                             - std::abs(pos.z - owp.z);
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
            }
            for (auto const& [_, child] : inst->getChildren()) self(self, child.get());
        };
        visit(visit, workspace);
        return pos;
    };

    // ===================================================
    //  Terrainブラシ適用（有効時はクリックでの選択・ドラッグより優先する）
    // ===================================================
    bool terrainBrushActive = m_terrainBrush && m_terrainBrush->active;
    static constexpr double kTerrainBrushInterval = 0.15;  // 1段ずつ盛れるように適用間隔を間引く
    static constexpr float  kTerrainBrushMaxDist  = 400.0f; // グレージング角で遠方を誤編集しないよう距離制限
    double now = ImGui::GetTime();
    if (terrainBrushActive && isHoveringViewport && user && workspace) {
        // Workspace 直下から Terrain を探す
        Terrain* terrain = nullptr;
        for (auto const& [name, child] : workspace->getChildren()) {
            if (child->IsA("Terrain")) { terrain = static_cast<Terrain*>(child.get()); break; }
        }
        if (terrain && terrain->Enabled && terrain->streamer) {
            // クリックでストローク開始。現在のUndo用差分バッファをこのTerrainに紐付けて記録開始する。
            if (ImGui::IsMouseClicked(0)) {
                m_terrainBrushDiff.clear();
                terrain->streamer->beginDiffCapture(&m_terrainBrushDiff);
                m_terrainBrushStrokeTarget = std::static_pointer_cast<Terrain>(terrain->shared_from_this());
            }

            ImVec2 mousePos = ImGui::GetMousePos();
            Vector3 rayDir = makeRay(mousePos.x - contentOrigin.x, mousePos.y - contentOrigin.y);
            Vector3 rayOri = camPos();

            // 物理シーンではなくブロックデータへ直接レイキャストする。常に最新の地形を
            // 参照するため、編集直後でも貫通（地中ワープ）が発生しない。
            // 面法線つきレイキャストで、ヒットした面の軸(axis)/符号(sign)を取得する。
            Vector3 hitPos;
            int32_t hitBx = 0, hitBy = 0, hitBz = 0, hitAxis = 1, hitSign = 1;
            if (terrain->streamer->raycastVoxelFace(rayOri, rayDir, kTerrainBrushMaxDist,
                                                     hitPos, hitBx, hitBy, hitBz, hitAxis, hitSign)) {
                // ヒット位置にブラシ範囲のガイドリングを描画（シーン描画済みFBOへ追記）。
                // クリック前でも「どこに当たるか」が見えるようマウス押下に関係なく毎フレーム描く。
                if (Renderer::instance && framebuffer) {
                    float   aspect = (h > 0) ? (float)w / (float)h : 1.0f;
                    Matrix4 proj   = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
                    Matrix4 view   = Matrix4::LookAt(camPos(), camPos() + camForward(), camUp());
                    GLint prevFBO = 0; GLint prevVp[4] = {};
                    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
                    glGetIntegerv(GL_VIEWPORT, prevVp);
                    Vector3 brushNormal(0.0f, 0.0f, 0.0f);
                    if (hitAxis == 0) brushNormal.x = (float)hitSign;
                    else if (hitAxis == 1) brushNormal.y = (float)hitSign;
                    else brushNormal.z = (float)hitSign;

                    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                    glViewport(0, 0, fbWidth, fbHeight);
                    Renderer::instance->renderBrushMarker(view, proj, hitPos, m_terrainBrush->radius, camPos(), brushNormal);
                    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
                    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
                }
                // 左ボタン押下中のみ実際に編集する（適用間隔で間引く）
                if (ImGui::IsMouseDown(0) &&
                    (m_lastTerrainBrushTime < 0.0 || now - m_lastTerrainBrushTime >= kTerrainBrushInterval)) {
                    if (m_terrainBrush->paintMode) {
                        terrain->streamer->applyColorBrush(hitPos, hitAxis, hitSign, m_terrainBrush->radius,
                            (uint8_t)(m_terrainBrush->paintColor[0] * 255.0f),
                            (uint8_t)(m_terrainBrush->paintColor[1] * 255.0f),
                            (uint8_t)(m_terrainBrush->paintColor[2] * 255.0f));
                    } else {
                        terrain->streamer->applyDirectionalBrush(hitPos, hitAxis, hitSign,
                            m_terrainBrush->radius, m_terrainBrush->mode);
                    }
                    m_lastTerrainBrushTime = now;
                }
            }
        }
    }

    // ストローク終了検知: ブラシがオフになっていてもマウスを離したら確実に確定させる。
    // isHoveringViewport 等に依存させず、ストローク中かどうかだけで判定する。
    if (ImGui::IsMouseReleased(0) && m_terrainBrushStrokeTarget) {
        if (m_terrainBrushStrokeTarget->streamer) m_terrainBrushStrokeTarget->streamer->endDiffCapture();
        if (!m_terrainBrushDiff.empty() && m_history) {
            m_history->record(std::make_unique<TerrainBrushStrokeCommand>(
                m_terrainBrushStrokeTarget, std::move(m_terrainBrushDiff)));
        }
        m_terrainBrushDiff.clear();
        m_terrainBrushStrokeTarget.reset();
    }

    // ---- クリック処理: 選択 & ドラッグ開始（全モード共通） ----
    // Selectモードでも非Selectモードでもレイキャストでオブジェクトを選択できる。
    // 非Selectモードでは現在の選択物をクリックしたときのみドラッグ開始する。
    if (!terrainBrushActive && !isNoToolMode() && isHoveringViewport && ImGui::IsMouseClicked(0) && !ImGuizmo::IsUsing() && user && workspace) {
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector3 rayDir = makeRay(mousePos.x - contentOrigin.x, mousePos.y - contentOrigin.y);
        Vector3 rayOri = camPos();

        // ---- ピッカーモード: Pick ボタン押下中はクリックをキューブ/Attachment指定に横取り ----
        if (m_picker && m_picker->active) {
            Instance* nearest = nullptr;
            float nearestT = 1e30f;
            const bool pickAtt = m_picker->pickAttachment;
            auto pickerCast = [&](auto& self, Instance* node) -> void {
                if (!node) return;
                if (pickAtt ? node->IsA("Attachment") : node->IsA("BaseCube")) {
                    auto* s = static_cast<Spatial*>(node);
                    // Attachment はサイズを持たないので、デバッグ描画のワイヤ球より
                    // 少し大きい固定サイズの当たり判定でクリックできるようにする
                    Vector3 hitSize = pickAtt ? Vector3(0.5f, 0.5f, 0.5f) : s->Size;
                    float t = obbHit(rayOri, rayDir, s->getWorldCFrame(), hitSize);
                    if (t >= 0.0f && t < nearestT) { nearestT = t; nearest = node; }
                }
                for (auto& [_, c] : node->getChildren()) self(self, c.get());
            };
            pickerCast(pickerCast, workspace);
            if (nearest && nearest != m_picker->constraint)
                m_picker->onPick(nearest->shared_from_this());
            m_picker->active     = false;
            m_isDraggingSelected = false;
            m_isBoxSelectArmed   = false;
        } else if (m_decalPlace && m_decalPlace->active && selectedInstance && *selectedInstance &&
                   (*selectedInstance)->IsA("MeshCube")) {
            // ---- Decal配置モード: MeshCube表面をクリックしたUV座標にDecalを自動配置 ----
            MeshCube* mc  = static_cast<MeshCube*>(*selectedInstance);
            CFrame    wcf = mc->getWorldCFrame();
            Quaternion invRot = wcf.Rotation.conjugate();

            Vector3 localOri = invRot.rotate(rayOri - wcf.Position) / mc->Size;
            Vector3 localDir = invRot.rotate(rayDir) / mc->Size;

            MeshHitResult hitResult = mc->raycastLocal(localOri, localDir);
            if (hitResult.hit && m_history) {
                auto mcSp = std::static_pointer_cast<Instance>(mc->shared_from_this());
                auto decal = std::make_shared<Decal>(0, Face::Front);
                decal->UVCenter = hitResult.uv;

                std::string name = "Decal";
                int n = 1;
                while (mc->children.count(name) > 0) name = "Decal" + std::to_string(n++);
                decal->Name = name;

                m_history->execute(std::make_unique<AddInstanceCommand>(mcSp, decal));
            }
            m_decalPlace->active  = false;
            m_isDraggingSelected  = false;
            m_isBoxSelectArmed    = false;
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
                if (inst->getClassName() == "Skybox") return;
                if (inst->IsA("BaseCube")) {
                    Spatial* s = static_cast<Spatial*>(inst);
                    float t = obbHit(rayOri, rayDir, s->getWorldCFrame(), s->Size);
                    if (t >= 0.0f && t < nearestT) { nearestT = t; nearest = inst; }
                }
                for (auto const& [_, child] : inst->getChildren())
                    self(self, child.get());
            };
            castRay(castRay, workspace);
            if (ImGui::GetIO().KeyCtrl && selectedInstances) {
                // Ctrl+クリック: 複数選択のトグル（ヒエラルキー側と同じセマンティクス）
                if (nearest) {
                    auto it = std::find(selectedInstances->begin(), selectedInstances->end(), nearest);
                    if (it != selectedInstances->end()) {
                        selectedInstances->erase(it);
                        if (*selectedInstance == nearest)
                            *selectedInstance = selectedInstances->empty() ? nullptr : selectedInstances->back();
                    } else {
                        selectedInstances->push_back(nearest);
                        *selectedInstance = nearest;
                    }
                }
                // ヒットなしの場合は選択を維持（何もしない）
            } else {
                *selectedInstance = nearest;
                if (selectedInstances) {
                    selectedInstances->clear();
                    if (nearest) selectedInstances->push_back(nearest);
                }
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
            Vector3 camTarget = camPos() + camForward();
            Matrix4 view = Matrix4::LookAt(camPos(), camTarget, camUp());
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
                if (!node || node->getClassName() == "Skybox") return;
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
    if (isGizmoMode() && selectedInstance && *selectedInstance && user) {
        Instance* inst = *selectedInstance;

        // ピボット自動解除: 対象・操作モードが変わったら Tab ピボットを無効化する
        if (m_pivotActive && (m_pivotOwner != inst || m_pivotOp != (int)gizmoOp))
            m_pivotActive = false;
        // ホールド式: Tab を離したらピボット解除。ただしドラッグ中はギズモ行列が
        // 途中で切り替わると delta 計算が破綻するため、ドラッグ終了まで維持する
        if (m_pivotActive && !ImGui::IsKeyDown(ImGuiKey_Tab) && !ImGuizmo::IsUsing())
            m_pivotActive = false;

        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);
            bool isModelTarget = inst->IsA("Model");
            // Model の SCALE はギズモを描画しない（子孫のBaseCubeを直接スケールする概念がないため）
            bool skipGizmoForModelScale = (isModelTarget && gizmoOp == ImGuizmo::SCALE);

            if (!skipGizmoForModelScale) {
            // Gizmo Undo: ドラッグ開始/終了を検知して MultiGizmoCommand を記録
            bool isUsingGizmo = ImGuizmo::IsUsing();

            if (!m_wasUsingGizmo && isUsingGizmo) {
                m_gizmoEntries.clear();
                // 複数選択中は全対象をキャプチャ、単一なら primary のみ
                std::vector<Instance*> targets = hasMultiSelection()
                    ? *selectedInstances : std::vector<Instance*>{ inst };
                for (Instance* tgt : targets) {
                    if (tgt && !tgt->Parent.expired() && tgt->IsA("Spatial")) {
                        Spatial* sp = static_cast<Spatial*>(tgt);
                        m_gizmoEntries.push_back({
                            std::static_pointer_cast<Spatial>(tgt->shared_from_this()),
                            { sp->Position, sp->Size, sp->Rotation }, {}
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
            Vector3 target = camPos() + camForward();
            Matrix4 view   = Matrix4::LookAt(camPos(), target, camUp());

            // Roblox スタイルリサイズ用: ドラッグ中でない間は常に最新状態を保持
            // IsUsing() は Manipulate() 呼び出し前の状態を返すため、
            // ドラッグ開始の最初フレームでは !isUsingGizmo が true になり正確な初期値を確保できる
            if (!isUsingGizmo && gizmoOp == ImGuizmo::SCALE) {
                m_scaleBeforeSize     = s->Size;
                m_scaleBeforeWorldPos = s->getWorldPosition();
            }

            // Model のバウンディングボックス中心（TRANSLATE/ROTATE のピボットに使う）
            Vector3 modelAabbMin(0.0f, 0.0f, 0.0f), modelAabbMax(0.0f, 0.0f, 0.0f);
            bool haveModelAABB = false;
            Vector3 modelPivotCenter;
            if (isModelTarget) {
                haveModelAABB   = computeDescendantWorldAABB(inst, modelAabbMin, modelAabbMax);
                modelPivotCenter = haveModelAABB ? (modelAabbMin + modelAabbMax) * 0.5f : s->getWorldPosition();
            }
            // ROTATE で使う Model の直前ワールド状態（Manipulate 呼び出し前に保存）
            Vector3    modelOldWorldPos = isModelTarget ? s->getWorldPosition()          : Vector3();
            Quaternion modelOldWorldRot = isModelTarget ? s->getWorldCFrame().Rotation   : Quaternion();

            // 複数選択の集合中心（TRANSLATE のみ、Model 以外）
            Vector3 multiCenter;
            bool haveMultiCenter = false;
            if (!isModelTarget && gizmoOp == ImGuizmo::TRANSLATE && hasMultiSelection()) {
                Vector3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
                bool any = false;
                for (Instance* o : *selectedInstances) {
                    if (o && !o->Parent.expired() && o->IsA("BaseCube")) {
                        accumulateWorldAABB(static_cast<Spatial*>(o), mn, mx);
                        any = true;
                    }
                }
                multiCenter     = any ? (mn + mx) * 0.5f : s->getWorldPosition();
                haveMultiCenter = true;
            }

            // SCALE ドラッグ中は開始時の不変行列を ImGuizmo に渡す
            // 毎フレーム変化する行列を渡すと ImGuizmo の内部参照がずれて特異点が生まれるため
            // FIX: それぞれの軸が干渉している
            Matrix4 model;
            if (isModelTarget) {
                // Model: 位置・回転のみ（Sizeベースのスケールはかけない）
                model = CFrame(modelPivotCenter, s->getWorldCFrame().Rotation).toMatrix4();
            } else if (m_pivotActive && gizmoOp == ImGuizmo::TRANSLATE) {
                // Tab ピボット: 位置=ピボット点、回転=Primaryのワールド回転、スケール=1
                model = CFrame(m_pivotWorld, s->getWorldCFrame().Rotation).toMatrix4();
            } else if (gizmoOp == ImGuizmo::TRANSLATE && haveMultiCenter) {
                // 複数選択: 位置=集合中心、回転=単位、スケール=1
                model = CFrame(multiCenter).toMatrix4();
            } else if (isUsingGizmo && gizmoOp == ImGuizmo::SCALE) {
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

            float snapArr[3]   = { snapTranslateVal, snapTranslateVal, snapTranslateVal };
            float rotSnap[3]   = { snapRotateVal,    snapRotateVal,    snapRotateVal    };
            const float* snap = nullptr;
            if      (gizmoOp == ImGuizmo::TRANSLATE && snapTranslate) snap = snapArr;
            else if (gizmoOp == ImGuizmo::ROTATE    && snapRotate)    snap = rotSnap;
            // SCALE snap は newSize 抽出後に絶対値で適用するため ImGuizmo には渡さない

            if (ImGuizmo::Manipulate(view.m, proj.m, gizmoOp,
                                     (gizmoOp == ImGuizmo::SCALE) ? ImGuizmo::WORLD : gizmoMode,
                                     model.m, nullptr, snap)) {
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

                if (isModelTarget && gizmoOp == ImGuizmo::TRANSLATE) {
                    // Model: AABB中心（無ければ自身のワールド位置）からの delta を Position に加算
                    Vector3 centerBefore = haveModelAABB ? modelPivotCenter : s->getWorldPosition();
                    Vector3 delta        = newPos - centerBefore;
                    Vector3 newWorldPos  = s->getWorldPosition() + delta;
                    s->Position = worldToLocal(newWorldPos, s);
                } else if (isModelTarget && gizmoOp == ImGuizmo::ROTATE) {
                    // Model: AABB中心（無ければ自身のワールド位置）を軸に回転を適用
                    Quaternion rotDelta   = newRot * modelOldWorldRot.conjugate();
                    Vector3    pivotCenter = haveModelAABB ? modelPivotCenter : modelOldWorldPos;
                    Vector3    newWorldPos = pivotCenter + rotDelta.rotate(modelOldWorldPos - pivotCenter);
                    Quaternion newWorldRot = rotDelta * modelOldWorldRot;
                    s->Position         = worldToLocal(newWorldPos, s);
                    s->cframe.Rotation  = worldToLocalRot(newWorldRot, s);
                } else if (m_pivotActive && gizmoOp == ImGuizmo::TRANSLATE) {
                    // Tab ピボット経路: delta を全選択対象に適用（collisionFit は使わない）
                    Vector3 delta = newPos - m_pivotWorld;
                    std::vector<Instance*> targets = hasMultiSelection()
                        ? *selectedInstances : std::vector<Instance*>{ inst };
                    for (Instance* tgt : targets) {
                        if (!tgt || tgt->Parent.expired() || !tgt->IsA("Spatial")) continue;
                        Spatial* tsp = static_cast<Spatial*>(tgt);
                        Vector3 newWorld = tsp->getWorldPosition() + delta;
                        Vector3 localP   = worldToLocal(newWorld, tsp);
                        if (tgt->IsA("BaseCube"))
                            static_cast<BaseCube*>(tgt)->teleportTo(localP);
                        else
                            tsp->Position = localP;
                    }
                    m_pivotWorld = m_pivotWorld + delta;
                } else if (gizmoOp == ImGuizmo::TRANSLATE && haveMultiCenter) {
                    // 複数選択中心経路: 集合中心からの delta を全選択対象に適用（collisionFit は使わない）
                    Vector3 delta = newPos - multiCenter;
                    for (Instance* other : *selectedInstances) {
                        if (!other || other->Parent.expired() || !other->IsA("BaseCube")) continue;
                        BaseCube* bc = static_cast<BaseCube*>(other);
                        Vector3 nw = bc->getWorldPosition() + delta;
                        bc->teleportTo(worldToLocal(nw, bc));
                    }
                } else if (gizmoOp == ImGuizmo::TRANSLATE && workspace) {
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
                    // 絶対サイズスナップ: 変化した軸のみスナップ（未変化軸は before 値を維持）
                    if (snapScale && snapScaleVal > 1e-6f) {
                        if (std::abs(newSize.x - m_scaleBeforeSize.x) >= 1e-4f)
                            newSize.x = (std::max)(std::round(newSize.x / snapScaleVal) * snapScaleVal, 0.05f);
                        else newSize.x = m_scaleBeforeSize.x;
                        if (std::abs(newSize.y - m_scaleBeforeSize.y) >= 1e-4f)
                            newSize.y = (std::max)(std::round(newSize.y / snapScaleVal) * snapScaleVal, 0.05f);
                        else newSize.y = m_scaleBeforeSize.y;
                        if (std::abs(newSize.z - m_scaleBeforeSize.z) >= 1e-4f)
                            newSize.z = (std::max)(std::round(newSize.z / snapScaleVal) * snapScaleVal, 0.05f);
                        else newSize.z = m_scaleBeforeSize.z;
                    }
                    // Roblox スタイル: size デルタの半分だけ position をオフセット
                    // 負方向ハンドルのときは符号を反転して逆面を固定する
                    Vector3 deltaSize = newSize - m_scaleBeforeSize;
                    // 固定面の符号は「掴み点が軸のどちら側か」のワールド幾何で決める（カメラ非依存）。
                    // 背面に回っても反転しない。単一軸以外は従来の IsScaleNegative にフォールバック。
                    auto grabSign = [](int ax) -> float {
                        float g = ImGuizmo::GetScaleGrabSign(ax);
                        return g != 0.0f ? g : (ImGuizmo::IsScaleNegative(ax) ? -1.0f : 1.0f);
                    };
                    float sx = grabSign(0);
                    float sy = grabSign(1);
                    float sz = grabSign(2);
                    // オフセットはオブジェクトのローカル軸に沿って行う。回転していても反対面が
                    // 正しく固定される（未回転ならワールド軸と一致＝従来と同等）
                    Quaternion wr = s->getWorldCFrame().Rotation;
                    Vector3 offset =
                        wr.rotate(Vector3(1, 0, 0)) * (deltaSize.x * sx) +
                        wr.rotate(Vector3(0, 1, 0)) * (deltaSize.y * sy) +
                        wr.rotate(Vector3(0, 0, 1)) * (deltaSize.z * sz);
                    Vector3 newWorldPos = m_scaleBeforeWorldPos + offset * 0.5f;
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
            } // end if (!skipGizmoForModelScale)
        }
    }

    // ---- 複数選択ハイライト: 全選択オブジェクトの OBB を画面投影してアウトラインを描画 ----
    if (user && selectedInstances && !selectedInstances->empty()) {
        float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
        Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
        Matrix4 view = Matrix4::LookAt(camPos(), camPos() + camForward(), camUp());
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

    // ---- Model ハイライト: 選択中が Model のとき、子孫 BaseCube の集合 AABB の12辺を描画 ----
    if (user && selectedInstance && *selectedInstance && (*selectedInstance)->IsA("Model")) {
        Vector3 aabbMin, aabbMax;
        if (computeDescendantWorldAABB(*selectedInstance, aabbMin, aabbMax)) {
            float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
            Matrix4 proj = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
            Matrix4 view = Matrix4::LookAt(camPos(), camPos() + camForward(), camUp());
            Matrix4 vp   = proj * view;
            const float* mv = vp.m;
            auto* dl = ImGui::GetWindowDrawList();

            Vector3 corners[8];
            for (int ci = 0; ci < 8; ++ci) {
                corners[ci] = Vector3(
                    (ci & 1) ? aabbMax.x : aabbMin.x,
                    (ci & 2) ? aabbMax.y : aabbMin.y,
                    (ci & 4) ? aabbMax.z : aabbMin.z
                );
            }
            auto project = [&](const Vector3& wc, bool& visible) -> ImVec2 {
                float cx = mv[0]*wc.x + mv[4]*wc.y + mv[8]*wc.z  + mv[12];
                float cy = mv[1]*wc.x + mv[5]*wc.y + mv[9]*wc.z  + mv[13];
                float cw = mv[3]*wc.x + mv[7]*wc.y + mv[11]*wc.z + mv[15];
                visible = (cw > 0.0f);
                if (!visible) return ImVec2(0, 0);
                float sx = contentOrigin.x + (cx/cw + 1.0f) * 0.5f * (float)w;
                float sy = contentOrigin.y + (1.0f - cy/cw) * 0.5f * (float)h;
                return ImVec2(sx, sy);
            };
            static const int kEdges[12][2] = {
                {0,1},{0,2},{0,4},{1,3},{1,5},{2,3},
                {2,6},{3,7},{4,5},{4,6},{5,7},{6,7}
            };
            ImU32 col = IM_COL32(255, 240, 80, 220); // primary色に合わせる
            for (auto& e : kEdges) {
                bool v0, v1;
                ImVec2 p0 = project(corners[e[0]], v0);
                ImVec2 p1 = project(corners[e[1]], v1);
                if (v0 && v1) dl->AddLine(p0, p1, col, 2.0f);
            }
        }
    }

    // ---- 自由移動ドラッグ開始/終了検出 ----
    bool wasDragging = m_wasDraggingSelected;

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
            if (tgt && !tgt->Parent.expired() && tgt->IsA("Spatial")) {
                Spatial* sp = static_cast<Spatial*>(tgt);
                m_freeDragEntries.push_back({
                    std::static_pointer_cast<Spatial>(tgt->shared_from_this()),
                    { sp->Position, sp->Size, sp->Rotation }, {}
                });
            }
        }
    }

    // 次フレームの比較用に今フレームの最終状態を保存する
    m_wasDraggingSelected = m_isDraggingSelected;

    // ---- Move モード自由移動: 選択キューブ上からのドラッグでサーフェスに追従 ----
    if (isMoveMode() && m_isDraggingSelected && !ImGuizmo::IsUsing()
            && selectedInstance && *selectedInstance && user && workspace) {
        Instance* inst = *selectedInstance;
        if (inst->IsA("Spatial")) {
            Spatial* s = static_cast<Spatial*>(inst);
            ImVec2 mp = ImGui::GetMousePos();
            Vector3 dir = makeRay(mp.x - contentOrigin.x, mp.y - contentOrigin.y);
            Vector3 ori = camPos();

            [&]() {
                // レイキャストなしは移動しない
                int   hitAxis = 1;
                float hitSign = 1.0f;
                Spatial* surface = castRaySurface(ori, dir, inst, hitAxis, hitSign);
                if (!surface) return;

                // 衝突面の法線軸 (hitAxis) に沿ってオブジェクトを隣接配置し、
                // 残り2軸はレイと軸平面の交点で決定する
                // → サーフェスが回転している場合があるため、サーフェスのローカル空間で計算する
                CFrame surfCF = surface->getWorldCFrame();
                Quaternion invSurf = surfCF.Rotation.conjugate();
                Quaternion movRot  = s->getWorldCFrame().Rotation;

                // レイをサーフェスローカルへ
                Vector3 lo = invSurf.rotate(ori - surfCF.Position);
                Vector3 ld = invSurf.rotate(dir);

                float surfHalf[3] = { surface->Size.x * 0.5f, surface->Size.y * 0.5f, surface->Size.z * 0.5f };

                // 面法線(ワールド): surfCF.Rotation.rotate(e_hitAxis) * hitSign
                Vector3 eHitAxis = (hitAxis == 0) ? Vector3(1.0f, 0.0f, 0.0f)
                                 : (hitAxis == 1) ? Vector3(0.0f, 1.0f, 0.0f)
                                                  : Vector3(0.0f, 0.0f, 1.0f);
                Vector3 faceNormalWorld = surfCF.Rotation.rotate(eHitAxis) * hitSign;

                // 移動物の面法線方向サポート半径(符号なし半径)
                float movSupportN = obbSupportRadius(movRot, s->Size, faceNormalWorld);

                // 固定ローカル座標
                float fixedCoord = hitSign * surfHalf[hitAxis] + hitSign * movSupportN;

                float loArr[3] = { lo.x, lo.y, lo.z };
                float ldArr[3] = { ld.x, ld.y, ld.z };

                // ローカルレイと平面 local[hitAxis] = fixedCoord の交点
                if (std::abs(ldArr[hitAxis]) < 1e-6f) return; // レイが面に平行
                float t = (fixedCoord - loArr[hitAxis]) / ldArr[hitAxis];
                if (t < 0.0f) return; // 平面がカメラ後方

                float localPosArr[3];
                localPosArr[hitAxis] = fixedCoord;
                for (int i = 0; i < 3; ++i) {
                    if (i == hitAxis) continue;
                    Vector3 eI = (i == 0) ? Vector3(1.0f, 0.0f, 0.0f)
                               : (i == 1) ? Vector3(0.0f, 1.0f, 0.0f)
                                          : Vector3(0.0f, 0.0f, 1.0f);
                    float movSupportI = obbSupportRadius(movRot, s->Size, surfCF.Rotation.rotate(eI));
                    float loLim = -surfHalf[i] + movSupportI;
                    float hiLim =  surfHalf[i] - movSupportI;
                    if (loLim > hiLim) { loLim = hiLim = 0.0f; } // サーフェスより移動物が大きい場合は面中央に固定
                    localPosArr[i] = std::clamp(loArr[i] + ldArr[i] * t, loLim, hiLim);
                }
                Vector3 surfLocalPos(localPosArr[0], localPosArr[1], localPosArr[2]);

                // ワールドへ戻す
                Vector3 newPos = surfCF.Position + surfCF.Rotation.rotate(surfLocalPos);

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

    // ---- Tab キー: ギズモをマウス位置へ移動するピボットを設定 ----
    if (isViewportFocused && ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::GetIO().WantTextInput
            && !ImGuizmo::IsUsing() && selectedInstance && *selectedInstance
            && (*selectedInstance)->IsA("Spatial") && user && workspace) {
        Instance* inst = *selectedInstance;
        Spatial*  s    = static_cast<Spatial*>(inst);

        ImVec2  mp  = ImGui::GetMousePos();
        Vector3 dir = makeRay(mp.x - contentOrigin.x, mp.y - contentOrigin.y);
        Vector3 ori = camPos();

        float nearestT = 1e30f;
        bool  hitAny   = false;
        auto pivotCast = [&](auto& self, Instance* node) -> void {
            if (!node) return;
            if (node->getClassName() == "Skybox") return;
            if (node->IsA("BaseCube")) {
                Spatial* sp = static_cast<Spatial*>(node);
                float t = obbHit(ori, dir, sp->getWorldCFrame(), sp->Size);
                if (t >= 0.0f && t < nearestT) { nearestT = t; hitAny = true; }
            }
            for (auto const& [_, child] : node->getChildren()) self(self, child.get());
        };
        pivotCast(pivotCast, workspace);

        if (hitAny) {
            m_pivotWorld = ori + dir * nearestT;
        } else {
            float dist = (s->getWorldPosition() - ori).length();
            m_pivotWorld = ori + dir * dist;
        }
        m_pivotActive = true;
        m_pivotOwner  = inst;
        m_pivotOp     = (int)gizmoOp;
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
            if (m_useOwnCamera) {
                m_camPos = objPos - camForward() * dist;
            } else {
                user->cpos = objPos - user->forward * dist;
                user->updateVectors();
            }
        }
    }

    ImGuizmo::PopID();
    ImGui::PopStyleVar();
    ImGui::End();
}
