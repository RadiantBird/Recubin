#include <Editor/ViewportPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <Editor/ViewportFocusManager.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/ViewportGeometry.hpp>
#include <Editor/ViewportSceneQueries.hpp>
#include <Math/Matrix4.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/ImGuizmo.h>
#include <Instances/BaseCube.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/System.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Weld.hpp>
#include <Core/SystemState.hpp>
#include <Core/Renderer.hpp>
#include <Core/Physics.hpp>
#include <Core/Terrain.hpp>
#include <Core/TerrainStreamer.hpp>
#include <Core/SceneRuntime.hpp>
#include <algorithm>
#include <cmath>

// NOTE: ImGuizmoは乗算でスケール処理を行っている

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

    const ViewportLayout layout = renderLayoutAndScene();
    updateViewportFocus();
    updateOwnCameraInput();
    const bool terrainBrushActive = updateTerrainBrush(layout);
    const bool weldModeConsumedClick = updateWeldMode(layout);
    handleViewportClick(layout, terrainBrushActive, weldModeConsumedClick);
    updateBoxSelection(layout);
    drawFocusBorder();
    updateGizmo(layout);
    drawHoverHighlight(layout);
    updateFreeDrag(layout);
    handlePivotShortcut(layout);
    handleFocusShortcut();

    ImGuizmo::PopID();
    ImGui::PopStyleVar();
    ImGui::End();
}

ViewportPanel::ViewportLayout ViewportPanel::renderLayoutAndScene() {
    ViewportLayout layout;

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
        desc.primarySelection  = selectedInstance ? *selectedInstance : nullptr;
        desc.selectionTargets  = selectedInstances;
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
        const GameGuiRenderContext guiContext = Renderer::makeGameGuiRenderContext(
            contentOrigin.x, contentOrigin.y, static_cast<float>(w), static_cast<float>(h),
            camPos(), camForward(), camRight(), camUp(),
            static_cast<float>(w) / static_cast<float>(h),
            /*recordUserViewport=*/!m_useOwnCamera);
        Renderer::instance->renderGameGui(*workspace, user, guiContext);
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

    layout.availableSize = avail;
    layout.panelOrigin = panelOrigin;
    layout.contentOrigin = contentOrigin;
    layout.width = w;
    layout.height = h;
    return layout;
}

void ViewportPanel::updateViewportFocus() {
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
}

void ViewportPanel::updateOwnCameraInput() {
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
}

ViewportGeometry::Ray ViewportPanel::makeMouseRay(const ViewportLayout& layout) const {
    const ImVec2 mousePosition = ImGui::GetMousePos();
    return ViewportGeometry::makeScreenRay(
        camPos(), camForward(), camRight(), camUp(),
        Vector2(mousePosition.x - layout.contentOrigin.x,
                mousePosition.y - layout.contentOrigin.y),
        Vector2(static_cast<float>(layout.width), static_cast<float>(layout.height)));
}

float ViewportPanel::scaleGrabSign(int axis) {
    const float sign = ImGuizmo::GetScaleGrabSign(axis);
    return sign != 0.0f ? sign : (ImGuizmo::IsScaleNegative(axis) ? -1.0f : 1.0f);
}

bool ViewportPanel::updateTerrainBrush(const ViewportLayout& layout) {
    const int w = layout.width;
    const int h = layout.height;

    // ===================================================
    //  Terrainブラシ適用（有効時はクリックでの選択・ドラッグより優先する）
    // ===================================================
    bool terrainBrushActive = m_terrainBrush && m_terrainBrush->active;
    static constexpr double kTerrainBrushInterval = 0.15;  // 1段ずつ盛れるように適用間隔を間引く
    static constexpr float  kTerrainBrushMaxDist  = 400.0f; // グレージング角で遠方を誤編集しないよう距離制限
    double now = ImGui::GetTime();
    if (terrainBrushActive && isHoveringViewport && user && workspace) {
        const ViewportGeometry::Ray ray = makeMouseRay(layout);
        const Vector3 rayDir = ray.direction;
        const Vector3 rayOri = ray.origin;
        Vector3 hitPos;
        int32_t hitBx = 0, hitBy = 0, hitBz = 0, hitAxis = 1, hitSign = 1;
        bool terrainHit = false;

        // ストローク中は開始時Terrainに固定。開始前は全Enabled
        // Terrainをraycastし、最近のヒットだけを対象にする。
        Terrain* terrain = m_terrainBrushStrokeTarget.get();
        if (terrain && terrain->Enabled && terrain->streamer) {
            terrainHit = terrain->streamer->raycastVoxelFace(
                rayOri, rayDir, kTerrainBrushMaxDist, hitPos,
                hitBx, hitBy, hitBz, hitAxis, hitSign);
        } else {
            terrain = nullptr;
            float nearestDistance = kTerrainBrushMaxDist + 1.0f;
            for (Terrain* candidate : SceneRuntime::collectTerrains(workspace)) {
                if (!candidate || !candidate->Enabled || !candidate->streamer) continue;
                Vector3 candidateHit;
                int32_t bx = 0, by = 0, bz = 0, axis = 1, sign = 1;
                if (!candidate->streamer->raycastVoxelFace(
                        rayOri, rayDir, kTerrainBrushMaxDist, candidateHit,
                        bx, by, bz, axis, sign)) continue;
                const float distance = (candidateHit - rayOri).length();
                if (distance >= nearestDistance) continue;
                nearestDistance = distance;
                terrain = candidate;
                hitPos = candidateHit;
                hitBx = bx; hitBy = by; hitBz = bz;
                hitAxis = axis; hitSign = sign;
                terrainHit = true;
            }
        }
        if (terrain && terrain->Enabled && terrain->streamer) {
            // クリックでストローク開始。現在のUndo用差分バッファをこのTerrainに紐付けて記録開始する。
            if (ImGui::IsMouseClicked(0)) {
                m_terrainBrushDiff.clear();
                terrain->streamer->beginDiffCapture(&m_terrainBrushDiff);
                m_terrainBrushStrokeTarget = std::static_pointer_cast<Terrain>(terrain->shared_from_this());
            }

            // 物理シーンではなくブロックデータへ直接レイキャストする。常に最新の地形を
            // 参照するため、編集直後でも貫通（地中ワープ）が発生しない。
            // 面法線つきレイキャストで、ヒットした面の軸(axis)/符号(sign)を取得する。
            if (terrainHit) {
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

    return terrainBrushActive;
}

bool ViewportPanel::updateWeldMode(const ViewportLayout& layout) {
    const int w = layout.width;
    const int h = layout.height;
    bool weldModeConsumedClick = false;
    if (m_weldMode && m_weldMode->active && isHoveringViewport && user && workspace) {
        weldModeConsumedClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing();
        const ViewportGeometry::Ray ray = makeMouseRay(layout);
        const ViewportSceneQueries::PickerRayHit hoverHit =
            ViewportSceneQueries::findPickerTarget(
                *workspace, ray, ViewportSceneQueries::PickerTargetType::BaseCube);
        BaseCube* hoveredCube = hoverHit.hit ? static_cast<BaseCube*>(hoverHit.target) : nullptr;

        if (hoveredCube && Renderer::instance && framebuffer) {
            const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
            const Matrix4 projection = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
            const Matrix4 view = Matrix4::LookAt(camPos(), camPos() + camForward(), camUp());
            GLint oldFbo = 0, oldViewport[4] = {};
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
            glGetIntegerv(GL_VIEWPORT, oldViewport);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(0, 0, fbWidth, fbHeight);
            Renderer::instance->drawTransientHighlight(
                hoveredCube, Color4(0.0f, 0.0f, 0.0f, 0.0f), Color4(0.15f, 1.0f, 0.25f, 1.0f), 3.0f,
                view, projection, camPos(), 45.0f, h);
            glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
            glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        }

        if (hoveredCube && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing()) {
            auto cube = std::static_pointer_cast<BaseCube>(hoveredCube->shared_from_this());
            auto cube0 = m_weldMode->cube0.lock();
            if (cube0 && cube0->findFirstAncestorWorkspace() != workspace) {
                cube0.reset();
                m_weldMode->cube0.reset();
            }
            if (!cube0) {
                m_weldMode->cube0 = cube;
            } else if (cube0 != cube && m_history) {
                auto weld = std::make_shared<Weld>();
                weld->setCube0(cube0);
                weld->setCube1(cube);
                weld->Name = SceneHierarchyPanel::uniqueName(cube0, "Weld");
                m_history->execute(std::make_unique<AddInstanceCommand>(cube0, weld));
                m_weldMode->cube0.reset();
            }
            m_isDraggingSelected = false;
            m_isFreeDragArmed = false;
            m_isBoxSelectArmed = false;
        }
    }

    return weldModeConsumedClick;
}

void ViewportPanel::handleViewportClick(
        const ViewportLayout& layout,
        bool terrainBrushActive,
        bool weldModeConsumedClick) {
    if (!terrainBrushActive && !weldModeConsumedClick && !isNoToolMode() && isHoveringViewport && ImGui::IsMouseClicked(0) && !ImGuizmo::IsUsing() && user && workspace) {
        const ViewportGeometry::Ray ray = makeMouseRay(layout);

        // ---- ピッカーモード: Pick ボタン押下中はクリックをキューブ/Attachment指定に横取り ----
        if (m_picker && m_picker->active) {
            if (!m_picker->pickClassName.empty()) return;
            const bool pickAtt = m_picker->pickAttachment;
            const ViewportSceneQueries::PickerRayHit pickerHit =
                ViewportSceneQueries::findPickerTarget(
                    *workspace,
                    ray,
                    pickAtt
                        ? ViewportSceneQueries::PickerTargetType::Attachment
                        : ViewportSceneQueries::PickerTargetType::BaseCube);
            Instance* nearest = pickerHit.hit ? pickerHit.target : nullptr;
            if (nearest && nearest != m_picker->constraint)
                m_picker->onPick(nearest->shared_from_this());
            m_picker->active     = false;
            m_isDraggingSelected = false;
            m_isFreeDragArmed    = false;
            m_isBoxSelectArmed   = false;
        } else if (m_decalPlace && m_decalPlace->active && selectedInstance && *selectedInstance &&
                   (*selectedInstance)->IsA("MeshCube")) {
            // ---- Decal配置モード: MeshCube表面をクリックしたUV座標にDecalを自動配置 ----
            MeshCube* mc  = static_cast<MeshCube*>(*selectedInstance);
            CFrame    wcf = mc->getWorldCFrame();
            Quaternion invRot = wcf.Rotation.conjugate();

            Vector3 localOri = invRot.rotate(ray.origin - wcf.Position) / mc->Size;
            Vector3 localDir = invRot.rotate(ray.direction) / mc->Size;

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
            m_isFreeDragArmed     = false;
            m_isBoxSelectArmed    = false;
        }
        else {
            // Step 1: 現在の選択単位の移動境界にヒットしたか判定
            bool hitSelected = false;
            if (selectedInstance && *selectedInstance &&
                    (*selectedInstance)->IsA("Spatial") &&
                    !ViewportSceneQueries::isLockedBaseCube(*selectedInstance)) {
                const ViewportSceneQueries::MovementBounds bounds =
                    ViewportSceneQueries::computeMovementBounds(**selectedInstance);
                if (bounds.valid) {
                    hitSelected = ViewportGeometry::raycastObb(
                        ray, CFrame(bounds.center, bounds.rotation), bounds.size).hit;
                }
            }
            m_isDraggingSelected = false;
            m_isFreeDragArmed = false;

            // Step 2: Selectモード、または非SelectモードでShift+クリックかつ
            // 現在の選択物にヒットしなかった場合は、クリック単位を問い合わせる。
            bool shiftHeld = ImGui::GetIO().KeyShift;
            bool ctrlHeld = ImGui::GetIO().KeyCtrl;
            bool clickFoundSomething = false;
            bool clickHitLockedObject = false;
            if ((selectOnly || (shiftHeld && !hitSelected)) && selectedInstance) {
                const ViewportSceneQueries::SelectionRayHit selectionHit =
                    ViewportSceneQueries::findSelectionTarget(*workspace, ray);
                Instance* nearest = selectionHit.hit ? selectionHit.target : nullptr;

                bool hitSomething = selectionHit.hit;
                bool hitLockedObject = selectionHit.locked;
                bool hitSelectableObject = hitSomething && nearest && !hitLockedObject;
                clickHitLockedObject = hitLockedObject;

                if (hitLockedObject) {
                    // 最前面がLockedなら背後のCubeは選択しない。
                    // 通常クリックでは選択解除、Ctrl+クリックでは現在の選択を維持する。
                    if (!ctrlHeld) {
                        *selectedInstance = nullptr;
                        if (selectedInstances) {
                            selectedInstances->clear();
                        }
                    }
                } else if (ctrlHeld && selectedInstances) {
                    if (hitSelectableObject) {
                        auto it = std::find(
                            selectedInstances->begin(),
                            selectedInstances->end(),
                            nearest
                        );

                        bool isAlreadySelected =
                            (it != selectedInstances->end());

                        if (isAlreadySelected) {
                            selectedInstances->erase(it);

                            if (*selectedInstance == nearest) {
                                if (selectedInstances->empty()) {
                                    *selectedInstance = nullptr;
                                } else {
                                    *selectedInstance = selectedInstances->back();
                                }
                            }
                        } else {
                            selectedInstances->push_back(nearest);
                            *selectedInstance = nearest;
                        }
                    }
                } else {
                    if (hitSelectableObject) {
                        const bool alreadySelected = selectedInstances &&
                            std::find(selectedInstances->begin(), selectedInstances->end(), nearest)
                                != selectedInstances->end();
                        *selectedInstance = nearest;

                        // 選択済み対象からのplain dragは複数選択を維持する。
                        if (selectedInstances && !alreadySelected) {
                            selectedInstances->clear();
                            selectedInstances->push_back(nearest);
                        }

                        if (isSelectMode() && !shiftHeld && nearest->IsA("Spatial")) {
                            m_isFreeDragArmed = true;
                            m_freeDragStart = ImGui::GetMousePos();
                        }
                    } else {
                        *selectedInstance = nullptr;

                        if (selectedInstances) {
                            selectedInstances->clear();
                        }
                    }
                }

                clickFoundSomething = hitSomething;
            } else if (isMoveMode() && hitSelected && !ctrlHeld && !shiftHeld) {
                // Moveモードも同じ5px閾値を経て表面ドラッグへ移る。
                m_isFreeDragArmed = true;
                m_freeDragStart = ImGui::GetMousePos();
            }

            // ボックス選択 arm: Selectモードのみ、何にもヒットしなかった場合、または
            // LockedなCubeにヒットした場合にドラッグで開始
            // 非Selectモードではギズモ操作と競合するため arm しない
            m_isBoxSelectArmed = isSelectMode() &&
                (!clickFoundSomething || clickHitLockedObject) && !shiftHeld;
            m_boxSelectStart   = ImGui::GetMousePos();
        } // end else (not picking)
    }
}

void ViewportPanel::updateBoxSelection(const ViewportLayout& layout) {
    const int w = layout.width;
    const int h = layout.height;
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

            ImVec2 cur = ImGui::GetMousePos();
            float minX = (std::min)(m_boxSelectStart.x, cur.x);
            float maxX = (std::max)(m_boxSelectStart.x, cur.x);
            float minY = (std::min)(m_boxSelectStart.y, cur.y);
            float maxY = (std::max)(m_boxSelectStart.y, cur.y);

            selectedInstances->clear();
            *selectedInstance = nullptr;
            *selectedInstances = ViewportSceneQueries::collectBoxSelectableCubes(
                *workspace,
                vp,
                Vector2(layout.contentOrigin.x, layout.contentOrigin.y),
                Vector2(static_cast<float>(w), static_cast<float>(h)),
                Vector2(minX, minY),
                Vector2(maxX, maxY));

            if (selectedInstances->empty()) {
                *selectedInstance = nullptr;
            } else {
                *selectedInstance = selectedInstances->front();
            }
        }
        m_isBoxSelecting   = false;
        m_isBoxSelectArmed = false;
    }
}

void ViewportPanel::drawFocusBorder() {
    if (isViewportFocused) {
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetWindowPos(),
            ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                   ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
            IM_COL32(0, 200, 255, 100),
            0.0f, 0, 2.0f
        );
    }
}

void ViewportPanel::updateGizmo(const ViewportLayout& layout) {
    const int w = layout.width;
    const int h = layout.height;
    const ImVec2 contentOrigin = layout.contentOrigin;
    if (isGizmoMode() && selectedInstance && *selectedInstance && user &&
            !ViewportSceneQueries::isLockedBaseCube(*selectedInstance)) {
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

            // 複数選択の集合中心（TRANSLATE/ROTATE、Model 以外）
            Vector3 multiCenter;
            bool haveMultiCenter = false;
            if (!isModelTarget && (gizmoOp == ImGuizmo::TRANSLATE || gizmoOp == ImGuizmo::ROTATE)
                && hasMultiSelection()) {
                ViewportGeometry::WorldAabb selectionAabb;
                for (Instance* o : *selectedInstances) {
                    if (o && !o->Parent.expired() && o->IsA("BaseCube") &&
                            !ViewportSceneQueries::isLockedBaseCube(o)) {
                        Spatial* selectedSpatial = static_cast<Spatial*>(o);
                        ViewportGeometry::accumulateWorldAabb(
                            selectionAabb,
                            selectedSpatial->getWorldCFrame(),
                            selectedSpatial->Size);
                    }
                }
                multiCenter = selectionAabb.valid
                    ? (selectionAabb.minimum + selectionAabb.maximum) * 0.5f
                    : s->getWorldPosition();
                haveMultiCenter = true;
            }

            if (!m_wasUsingGizmo && isUsingGizmo) {
                m_gizmoEntries.clear();
                // 複数選択中は全対象をキャプチャ、単一なら primary のみ
                std::vector<Instance*> targets = hasMultiSelection()
                    ? *selectedInstances : std::vector<Instance*>{ inst };
                for (Instance* tgt : targets) {
                    if (tgt && !tgt->Parent.expired() && tgt->IsA("Spatial") &&
                            !ViewportSceneQueries::isLockedBaseCube(tgt)) {
                        Spatial* sp = static_cast<Spatial*>(tgt);
                        m_gizmoEntries.push_back({
                            std::static_pointer_cast<Spatial>(tgt->shared_from_this()),
                            { sp->Position, sp->Size, sp->Rotation }, {}
                        });
                    }
                }
                if (gizmoOp == ImGuizmo::ROTATE && haveMultiCenter) {
                    m_multiRotatePivot = multiCenter;
                    m_multiRotateGizmoStartRot = (gizmoMode == ImGuizmo::LOCAL)
                        ? s->getWorldCFrame().Rotation
                        : Quaternion();
                    m_multiRotateGizmoCurRot = m_multiRotateGizmoStartRot;
                    m_multiRotatePivotActive = true;
                }
            }
            if (m_wasUsingGizmo && !isUsingGizmo) {
                m_multiRotatePivotActive = false;
                if (m_history && !m_gizmoEntries.empty()) {
                    for (auto& e : m_gizmoEntries) {
                        if (e.target && !e.target->Parent.expired())
                            e.after = { e.target->Position, e.target->Size, e.target->Rotation };
                    }
                    m_history->record(std::make_unique<MultiGizmoCommand>(std::move(m_gizmoEntries)));
                    m_gizmoEntries.clear();
                }
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
            ViewportGeometry::WorldAabb modelAabb;
            Vector3 modelPivotCenter;
            if (isModelTarget) {
                modelAabb = ViewportSceneQueries::computeDescendantWorldAabb(*inst);
                modelPivotCenter = modelAabb.valid
                    ? (modelAabb.minimum + modelAabb.maximum) * 0.5f
                    : s->getWorldPosition();
            }
            // ROTATE で使う Model の直前ワールド状態（Manipulate 呼び出し前に保存）
            Vector3    modelOldWorldPos = isModelTarget ? s->getWorldPosition()          : Vector3();
            Quaternion modelOldWorldRot = isModelTarget ? s->getWorldCFrame().Rotation   : Quaternion();

            // SCALE ドラッグ中は開始時の不変行列を ImGuizmo に渡す
            // 毎フレーム変化する行列を渡すと ImGuizmo の内部参照がずれて特異点が生まれるため
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
            } else if (gizmoOp == ImGuizmo::ROTATE && haveMultiCenter) {
                // 複数選択: 位置=集合中心、回転=ギズモ状態
                Vector3 pivot = m_multiRotatePivotActive ? m_multiRotatePivot : multiCenter;
                Quaternion gizmoRot = m_multiRotatePivotActive
                    ? m_multiRotateGizmoCurRot
                    : ((gizmoMode == ImGuizmo::LOCAL) ? s->getWorldCFrame().Rotation : Quaternion());
                model = CFrame(pivot, gizmoRot).toMatrix4();
            } else if (gizmoOp == ImGuizmo::SCALE) {
                CFrame stableCF = s->getWorldCFrame();
                stableCF.Position = m_scaleBeforeWorldPos;
                // ImGuizmoの倍率をそのままSizeへ掛けず、1.0からの差を
                // ワールド単位の加算量として解釈するため単位スケールを渡す。
                model = stableCF.toMatrix4();
            } else {
                model = s->getWorldCFrame().toMatrix4() *
                        Matrix4::Scale(s->Size.x, s->Size.y, s->Size.z);
            }

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetGizmoSizeClipSpace(std::clamp(user->gizmoSize, 0.05f, 0.50f));

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
                Vector3 newSize = ViewportGeometry::additiveResize(
                    m_scaleBeforeSize,
                    Vector3(sx, sy, sz),
                    snapScale,
                    snapScaleVal);

                // 正規化した回転行列からクォータニオンを抽出
                float rotM[16] = {0};
                if (sx > 1e-6f) { rotM[0]=model.m[0]/sx; rotM[1]=model.m[1]/sx; rotM[2]=model.m[2]/sx; }
                if (sy > 1e-6f) { rotM[4]=model.m[4]/sy; rotM[5]=model.m[5]/sy; rotM[6]=model.m[6]/sy; }
                if (sz > 1e-6f) { rotM[8]=model.m[8]/sz; rotM[9]=model.m[9]/sz; rotM[10]=model.m[10]/sz; }
                rotM[15] = 1.0f;
                Quaternion newRot = Quaternion::FromRotationMatrix(rotM);

                if (isModelTarget && gizmoOp == ImGuizmo::TRANSLATE) {
                    // Model: AABB中心（無ければ自身のワールド位置）からの delta を Position に加算
                    Vector3 centerBefore = modelAabb.valid ? modelPivotCenter : s->getWorldPosition();
                    Vector3 fittedCenter = newPos;
                    const ViewportSceneQueries::MovementBounds bounds =
                        ViewportSceneQueries::computeMovementBounds(*inst);
                    if (collisionFit && workspace && bounds.valid) {
                        float rx = std::abs(newPos.x - centerBefore.x) > 1e-5f
                            ? ViewportSceneQueries::fitOnAxis(
                                *workspace, {newPos.x, centerBefore.y, centerBefore.z}, bounds, *inst, 0)
                            : centerBefore.x;
                        float ry = std::abs(newPos.y - centerBefore.y) > 1e-5f
                            ? ViewportSceneQueries::fitOnAxis(
                                *workspace, {rx, newPos.y, centerBefore.z}, bounds, *inst, 1)
                            : centerBefore.y;
                        float rz = std::abs(newPos.z - centerBefore.z) > 1e-5f
                            ? ViewportSceneQueries::fitOnAxis(
                                *workspace, {rx, ry, newPos.z}, bounds, *inst, 2)
                            : centerBefore.z;
                        fittedCenter = Vector3(rx, ry, rz);
                    }
                    Vector3 delta        = fittedCenter - centerBefore;
                    Vector3 newWorldPos  = s->getWorldPosition() + delta;
                    s->Position = ViewportGeometry::worldToLocalPosition(newWorldPos, *s);
                } else if (isModelTarget && gizmoOp == ImGuizmo::ROTATE) {
                    // Model: AABB中心（無ければ自身のワールド位置）を軸に回転を適用
                    Quaternion rotDelta   = newRot * modelOldWorldRot.conjugate();
                    Vector3    pivotCenter = modelAabb.valid ? modelPivotCenter : modelOldWorldPos;
                    Vector3    newWorldPos = pivotCenter + rotDelta.rotate(modelOldWorldPos - pivotCenter);
                    Quaternion newWorldRot = rotDelta * modelOldWorldRot;
                    s->Position = ViewportGeometry::worldToLocalPosition(newWorldPos, *s);
                    s->cframe.Rotation = ViewportGeometry::worldToLocalRotation(newWorldRot, *s);
                } else if (m_pivotActive && gizmoOp == ImGuizmo::TRANSLATE) {
                    // Tab ピボット経路: delta を全選択対象に適用（collisionFit は使わない）
                    Vector3 delta = newPos - m_pivotWorld;
                    std::vector<Instance*> targets = hasMultiSelection()
                        ? *selectedInstances : std::vector<Instance*>{ inst };
                    for (Instance* tgt : targets) {
                        if (!tgt || tgt->Parent.expired() || !tgt->IsA("Spatial") ||
                                ViewportSceneQueries::isLockedBaseCube(tgt)) continue;
                        Spatial* tsp = static_cast<Spatial*>(tgt);
                        Vector3 newWorld = tsp->getWorldPosition() + delta;
                        Vector3 localP = ViewportGeometry::worldToLocalPosition(newWorld, *tsp);
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
                        if (!other || other->Parent.expired() || !other->IsA("BaseCube") ||
                                ViewportSceneQueries::isLockedBaseCube(other)) continue;
                        BaseCube* bc = static_cast<BaseCube*>(other);
                        Vector3 nw = bc->getWorldPosition() + delta;
                        bc->teleportTo(ViewportGeometry::worldToLocalPosition(nw, *bc));
                    }
                } else if (gizmoOp == ImGuizmo::TRANSLATE && workspace) {
                    // teleportTo 前のワールド座標を保存（複数選択の delta 計算用）
                    Vector3 prevPrimaryWorld = s->getWorldPosition();

                    // newPos はワールド座標
                    if (collisionFit) {
                        Vector3 prevWorld = prevPrimaryWorld;
                        const ViewportSceneQueries::MovementBounds bounds =
                            ViewportSceneQueries::computeMovementBounds(*inst);
                        float rx = (std::abs(newPos.x - prevWorld.x) > 1e-5f)
                                   ? ViewportSceneQueries::fitOnAxis(
                                         *workspace, {newPos.x, prevWorld.y, prevWorld.z}, bounds, *inst, 0)
                                   : prevWorld.x;
                        float ry = (std::abs(newPos.y - prevWorld.y) > 1e-5f)
                                   ? ViewportSceneQueries::fitOnAxis(
                                         *workspace, {rx, newPos.y, prevWorld.z}, bounds, *inst, 1)
                                   : prevWorld.y;
                        float rz = (std::abs(newPos.z - prevWorld.z) > 1e-5f)
                                   ? ViewportSceneQueries::fitOnAxis(
                                         *workspace, {rx, ry, newPos.z}, bounds, *inst, 2)
                                   : prevWorld.z;
                        newPos = Vector3(rx, ry, rz);
                    }
                    // ワールド → ローカルに変換して設定
                    Vector3 localPos = ViewportGeometry::worldToLocalPosition(newPos, *s);
                    if (inst->IsA("BaseCube"))
                        static_cast<BaseCube*>(inst)->teleportTo(localPos);
                    else
                        s->Position = localPos;

                    // 複数選択: primary の delta を残りのオブジェクトに適用
                    if (hasMultiSelection()) {
                        Vector3 deltaWorld = s->getWorldPosition() - prevPrimaryWorld;
                        for (Instance* other : *selectedInstances) {
                            if (!other || other->Parent.expired() || other == inst ||
                                    !other->IsA("BaseCube") ||
                                    ViewportSceneQueries::isLockedBaseCube(other)) continue;
                            BaseCube* bc = static_cast<BaseCube*>(other);
                            Vector3 nw = bc->getWorldPosition() + deltaWorld;
                            bc->teleportTo(ViewportGeometry::worldToLocalPosition(nw, *bc));
                        }
                    }
                } else if (gizmoOp == ImGuizmo::SCALE) {
                    // Roblox スタイル: size デルタの半分だけ position をオフセット
                    // 負方向ハンドルのときは符号を反転して逆面を固定する
                    Vector3 deltaSize = newSize - m_scaleBeforeSize;
                    // 固定面の符号は「掴み点が軸のどちら側か」のワールド幾何で決める（カメラ非依存）。
                    // 背面に回っても反転しない。単一軸以外は従来の IsScaleNegative にフォールバック。
                    float signX = scaleGrabSign(0);
                    float signY = scaleGrabSign(1);
                    float signZ = scaleGrabSign(2);
                    // オフセットはオブジェクトのローカル軸に沿って行う。回転していても反対面が
                    // 正しく固定される（未回転ならワールド軸と一致＝従来と同等）
                    Quaternion wr = s->getWorldCFrame().Rotation;
                    Vector3 offset =
                        wr.rotate(Vector3(1, 0, 0)) * (deltaSize.x * signX) +
                        wr.rotate(Vector3(0, 1, 0)) * (deltaSize.y * signY) +
                        wr.rotate(Vector3(0, 0, 1)) * (deltaSize.z * signZ);
                    Vector3 newWorldPos = m_scaleBeforeWorldPos + offset * 0.5f;
                    Vector3 localPos = ViewportGeometry::worldToLocalPosition(newWorldPos, *s);
                    if (inst->IsA("BaseCube")) {
                        BaseCube* bc = static_cast<BaseCube*>(inst);
                        bc->teleportTo(localPos);
                        bc->setSize(newSize);
                    } else {
                        s->Position = localPos;
                        s->Size = newSize;
                    }
                } else if (gizmoOp == ImGuizmo::ROTATE && haveMultiCenter) {
                    // 複数選択中心経路: 集合中心を軸に全選択 Spatial を回転
                    if (m_gizmoEntries.empty()) {
                        for (Instance* tgt : *selectedInstances) {
                            if (tgt && !tgt->Parent.expired() && tgt->IsA("Spatial") &&
                                    !ViewportSceneQueries::isLockedBaseCube(tgt)) {
                                Spatial* sp = static_cast<Spatial*>(tgt);
                                m_gizmoEntries.push_back({
                                    std::static_pointer_cast<Spatial>(tgt->shared_from_this()),
                                    { sp->Position, sp->Size, sp->Rotation }, {}
                                });
                            }
                        }
                        if (!m_multiRotatePivotActive) {
                            m_multiRotatePivot = multiCenter;
                            m_multiRotateGizmoStartRot = (gizmoMode == ImGuizmo::LOCAL)
                                ? s->getWorldCFrame().Rotation
                                : Quaternion();
                            m_multiRotateGizmoCurRot = m_multiRotateGizmoStartRot;
                            m_multiRotatePivotActive = true;
                        }
                    }
                    Quaternion rotDelta = newRot * m_multiRotateGizmoStartRot.conjugate();
                    Vector3 pivot = m_multiRotatePivotActive ? m_multiRotatePivot : multiCenter;
                    for (auto& e : m_gizmoEntries) {
                        if (!e.target || e.target->Parent.expired()) continue;
                        CFrame localBefore(e.before.position, e.before.rotation);
                        CFrame worldStart;
                        auto par = e.target->Parent.lock();
                        if (par && par->IsA("Spatial")) {
                            worldStart = static_cast<Spatial*>(par.get())->getWorldCFrame() * localBefore;
                        } else {
                            worldStart = localBefore;
                        }
                        Vector3 newWorldPos = pivot + rotDelta.rotate(worldStart.Position - pivot);
                        Quaternion newWorldRot = rotDelta * worldStart.Rotation;
                        if (e.target->IsA("BaseCube")) {
                            BaseCube* bc = static_cast<BaseCube*>(e.target.get());
                            bc->teleportTo(ViewportGeometry::worldToLocalPosition(newWorldPos, *bc));
                            bc->setRotation(ViewportGeometry::worldToLocalRotation(newWorldRot, *bc));
                        } else {
                            e.target->Position = ViewportGeometry::worldToLocalPosition(
                                newWorldPos, *e.target);
                            e.target->cframe.Rotation = ViewportGeometry::worldToLocalRotation(
                                newWorldRot, *e.target);
                        }
                    }
                    m_multiRotateGizmoCurRot = newRot;
                } else if (gizmoOp == ImGuizmo::ROTATE) {
                    // newRot はワールド回転 → ローカルに変換
                    Quaternion localRot = ViewportGeometry::worldToLocalRotation(newRot, *s);
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
}

void ViewportPanel::drawHoverHighlight(const ViewportLayout& layout) {
    const int w = layout.width;
    const int h = layout.height;
    if ((!isSelectMode() && !isGizmoMode()) || !isHoveringViewport || ImGuizmo::IsUsing()
            || (isGizmoMode() && ImGuizmo::IsOver(gizmoOp)) || !user || !workspace
            || !Renderer::instance || !framebuffer
            || (m_weldMode && m_weldMode->active)
            || (m_terrainBrush && m_terrainBrush->active)
            || (m_picker && m_picker->active)
            || (m_decalPlace && m_decalPlace->active)) {
        return;
    }

    const ViewportSceneQueries::SelectionRayHit hoverHit =
        ViewportSceneQueries::findSelectionTarget(*workspace, makeMouseRay(layout));
    if (!hoverHit.hit || hoverHit.locked || !hoverHit.target) {
        return;
    }
    if (selectedInstances && std::find(
            selectedInstances->begin(), selectedInstances->end(), hoverHit.target)
            != selectedInstances->end()) {
        return;
    }

    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    const Matrix4 projection = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
    const Matrix4 view = Matrix4::LookAt(camPos(), camPos() + camForward(), camUp());
    GLint oldFbo = 0, oldViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, w, h);
    for (BaseCube* cube : ViewportSceneQueries::collectHighlightBaseCubes(*hoverHit.target)) {
        Renderer::instance->drawTransientHighlight(
            cube,
            Color4(0.0f, 0.0f, 0.0f, 0.0f),
            Color4(1.0f, 1.0f, 1.0f, 0.45f),
            1.5f,
            view, projection, camPos(), 45.0f, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}

void ViewportPanel::updateFreeDrag(const ViewportLayout& layout) {
    bool wasDragging = m_wasDraggingSelected;

    // ボタンを離したらリセット
    if (!ImGui::IsMouseDown(0)) {
        m_isDraggingSelected = false;
        m_isFreeDragArmed = false;
    } else if (m_isFreeDragArmed && !m_isDraggingSelected) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float dx = mouse.x - m_freeDragStart.x;
        const float dy = mouse.y - m_freeDragStart.y;
        if (dx * dx + dy * dy > 25.0f) {
            m_isDraggingSelected = true;
            m_isFreeDragArmed = false;
        }
    }

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
            if (tgt && !tgt->Parent.expired() && tgt->IsA("Spatial") &&
                    !ViewportSceneQueries::isLockedBaseCube(tgt)) {
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

    moveFreeDragSelection(layout);
}

void ViewportPanel::moveFreeDragSelection(const ViewportLayout& layout) {
    if ((!isMoveMode() && !isSelectMode()) || !m_isDraggingSelected || ImGuizmo::IsUsing()
            || !selectedInstance || !*selectedInstance || !user || !workspace
            || ViewportSceneQueries::isLockedBaseCube(*selectedInstance)) {
        return;
    }
    Instance* inst = *selectedInstance;
    if (!inst->IsA("Spatial")) {
        return;
    }
    Spatial* s = static_cast<Spatial*>(inst);
    const ViewportSceneQueries::MovementBounds movingBounds =
        ViewportSceneQueries::computeMovementBounds(*inst);
    if (!movingBounds.valid) {
        return;
    }
    const ViewportGeometry::Ray ray = makeMouseRay(layout);
    const ViewportSceneQueries::BaseCubeRayHit surfaceHit =
        ViewportSceneQueries::findNearestBaseCube(*workspace, ray, inst);
    if (!surfaceHit.hit || !surfaceHit.cube) {
        return;
    }
    Spatial* surface = surfaceHit.cube;
    const int hitAxis = surfaceHit.obb.axis;
    const float hitSign = surfaceHit.obb.sign;
    const Vector3 ori = ray.origin;
    const Vector3 dir = ray.direction;

                // 衝突面の法線軸 (hitAxis) に沿ってオブジェクトを隣接配置し、
                // 残り2軸はレイと軸平面の交点で決定する
                // → サーフェスが回転している場合があるため、サーフェスのローカル空間で計算する
                CFrame surfCF = surface->getWorldCFrame();
                Quaternion invSurf = surfCF.Rotation.conjugate();
                Quaternion movRot  = movingBounds.rotation;

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
                float movSupportN = ViewportGeometry::obbSupportRadius(
                    movRot, movingBounds.size, faceNormalWorld);

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
                    float movSupportI = ViewportGeometry::obbSupportRadius(
                        movRot, movingBounds.size, surfCF.Rotation.rotate(eI));
                    float loLim = -surfHalf[i] + movSupportI;
                    float hiLim =  surfHalf[i] - movSupportI;
                    if (loLim > hiLim) { loLim = hiLim = 0.0f; } // サーフェスより移動物が大きい場合は面中央に固定
                    localPosArr[i] = std::clamp(loArr[i] + ldArr[i] * t, loLim, hiLim);
                }
                Vector3 surfLocalPos(localPosArr[0], localPosArr[1], localPosArr[2]);

                // ワールドへ戻す
                Vector3 newCenter = surfCF.Position + surfCF.Rotation.rotate(surfLocalPos);

                if (collisionFit) {
                    newCenter = ViewportSceneQueries::fitCollision(
                        *workspace, newCenter, movingBounds, *inst);
                }
                Vector3 prevPrimaryWorld = s->getWorldPosition();
                Vector3 newRootWorld = prevPrimaryWorld + (newCenter - movingBounds.center);
                Vector3 localPos = ViewportGeometry::worldToLocalPosition(newRootWorld, *s);
                if (inst->IsA("BaseCube"))
                    static_cast<BaseCube*>(inst)->teleportTo(localPos);
                else
                    s->Position = localPos;

                // 複数選択: primary の delta を残りのオブジェクトに適用
                if (hasMultiSelection()) {
                    Vector3 deltaWorld = s->getWorldPosition() - prevPrimaryWorld;
                    for (Instance* other : *selectedInstances) {
                        if (!other || other->Parent.expired() || other == inst ||
                                !other->IsA("Spatial") ||
                                ViewportSceneQueries::isLockedBaseCube(other)) continue;
                        Spatial* otherSpatial = static_cast<Spatial*>(other);
                        Vector3 nw = otherSpatial->getWorldPosition() + deltaWorld;
                        Vector3 otherLocal = ViewportGeometry::worldToLocalPosition(nw, *otherSpatial);
                        if (other->IsA("BaseCube"))
                            static_cast<BaseCube*>(other)->teleportTo(otherLocal);
                        else
                            otherSpatial->Position = otherLocal;
                    }
                }
}

void ViewportPanel::handlePivotShortcut(const ViewportLayout& layout) {
    if (isViewportFocused && ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::GetIO().WantTextInput
            && !ImGuizmo::IsUsing() && selectedInstance && *selectedInstance
            && (*selectedInstance)->IsA("Spatial") && user && workspace &&
            !ViewportSceneQueries::isLockedBaseCube(*selectedInstance)) {
        Instance* inst = *selectedInstance;
        Spatial*  s    = static_cast<Spatial*>(inst);

        const ViewportGeometry::Ray ray = makeMouseRay(layout);
        const ViewportSceneQueries::BaseCubeRayHit pivotHit =
            ViewportSceneQueries::findNearestBaseCube(*workspace, ray);
        if (pivotHit.hit) {
            m_pivotWorld = ray.origin + ray.direction * pivotHit.obb.distance;
        } else {
            float dist = (s->getWorldPosition() - ray.origin).length();
            m_pivotWorld = ray.origin + ray.direction * dist;
        }
        m_pivotActive = true;
        m_pivotOwner  = inst;
        m_pivotOp     = (int)gizmoOp;
    }
}

void ViewportPanel::handleFocusShortcut() {
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
}
