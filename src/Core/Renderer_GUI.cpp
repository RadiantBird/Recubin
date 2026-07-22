#include "include/Core/Renderer.hpp"
#include "include/Core/SystemState.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/GuiButton.hpp"
#include "include/Instances/WorldGuiObject.hpp"
#include "include/Instances/SurfaceGui.hpp"
#include "include/Instances/BillboardGui.hpp"
#include "include/Instances/ProximityPrompt.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/Instances/System.hpp"
#include "include/Instances/ChatService.hpp"
#include "include/imgui/imgui.h"
#include "include/imgui/imgui_impl_opengl3.h"

#include <algorithm>
#include <vector>

void Renderer::renderRuntimeChat(float vpX, float vpY, float vpW, float vpH) {
    if (auto service = m_chatService.lock()) m_chatOverlay.render(*service, vpX, vpY, vpW, vpH);
}

// ===================================================
//  ScreenGuiObject の再帰収集
// ===================================================
static void collectScreenGui(Instance* node, std::vector<ScreenGuiObject*>& out) {
    for (auto& [name, child] : node->getChildren()) {
        // WorldGuiObject (SurfaceGui, BillboardGui等) の子はベイク専用なのでスキップ
        if (child->IsA("WorldGuiObject")) continue;
        if (child->IsA("ScreenGuiObject")) {
            out.push_back(static_cast<ScreenGuiObject*>(child.get()));
        }
        collectScreenGui(child.get(), out);
    }
}

// SurfaceGui/BillboardGui のホストをWorkspace直下に限定しない。
// Model/Folderなどの下にあるBaseCubeも、ワールドGUIの描画対象に含める。
static void collectWorldGuiHosts(Instance* node, std::vector<BaseCube*>& out) {
    if (!node) return;
    if (node->IsA("BaseCube")) {
        out.push_back(static_cast<BaseCube*>(node));
    }
    for (auto const& [name, child] : node->getChildren()) {
        collectWorldGuiHosts(child.get(), out);
    }
}

// ===================================================
//  ScreenGui 1 要素の描画
// ===================================================
// テキストを縦中央に描画。FontSize>0 なら指定サイズ、0 なら既定サイズを使う
static void drawGuiText(ImDrawList* dl, ScreenGuiObject* sgo,
                        float px, float py, float sh, ImU32 col, const char* text) {
    float size = (sgo->FontSize > 0.f) ? sgo->FontSize : ImGui::GetFontSize();
    dl->AddText(ImGui::GetFont(), size, ImVec2(px + 4.f, py + (sh - size) * 0.5f), col, text);
}

static ImU32 toImCol(const Color4& c) {
    return IM_COL32((int)(c.r*255), (int)(c.g*255), (int)(c.b*255), (int)(c.a*255));
}

// 背景+画像+文字+（任意）ボタン当たり判定の共通描画。
// Screen直描画 / SurfaceGuiベイク / BillboardGuiパネル の3系統で共用する。
// onActivated が null のときは非対話（ベイク用: InvisibleButton を発行しない）
static void drawGuiContent(ImDrawList* dl, ScreenGuiObject* sgo,
                           float px, float py, float sw, float sh,
                           std::function<void(GuiButton*)>* onActivated) {
    ImVec2 tl(px, py), br(px + sw, py + sh);
    dl->AddRectFilled(tl, br, toImCol(sgo->BackgroundColor));
    if (auto* img = sgo->imageContent(); img && img->textureID != 0)
        dl->AddImage((ImTextureID)(uintptr_t)img->textureID, tl, br,
                     ImVec2(0, 1), ImVec2(1, 0));  // 上下反転（テクスチャ原点補正）
    if (auto* txt = sgo->textContent(); txt && !txt->Text.empty())
        drawGuiText(dl, sgo, px, py, sh, toImCol(txt->TextColor), txt->Text.c_str());
    if (onActivated && *onActivated && sgo->Active && sgo->IsA("GuiButton")) {
        // ID はインスタンスポインタ由来（同名インスタンスの ID 衝突防止）
        ImGui::SetCursorScreenPos(tl);
        std::string btnId = "##btn_" + std::to_string(reinterpret_cast<uintptr_t>(sgo));
        ImGui::InvisibleButton(btnId.c_str(), ImVec2(sw, sh));
        if (ImGui::IsItemClicked())
            (*onActivated)(static_cast<GuiButton*>(sgo));
    }
}

static void drawScreenGuiElement(ImDrawList* dl, ScreenGuiObject* sgo,
                                  float vpX, float vpY, float vpW, float vpH,
                                  float scaleX, float scaleY,
                                  std::function<void(GuiButton*)>& onActivated) {
    if (!sgo->Visible) return;

    float px = (sgo->NormType == Norm::Scale) ? sgo->Position.x * vpW + vpX : sgo->Position.x * scaleX + vpX;
    float py = (sgo->NormType == Norm::Scale) ? sgo->Position.y * vpH + vpY : sgo->Position.y * scaleY + vpY;
    float sw = (sgo->NormType == Norm::Scale) ? sgo->Size.x * vpW : sgo->Size.x * scaleX;
    float sh = (sgo->NormType == Norm::Scale) ? sgo->Size.y * vpH : sgo->Size.y * scaleY;

    ImVec2 tl(px, py);
    ImVec2 br(px + sw, py + sh);

    drawGuiContent(dl, sgo, px, py, sw, sh, &onActivated);

    // ホバー判定（TextLabel/TextButton 共通）: 入った瞬間に Hovered を発火
    if (SystemState::get().isPlaying) {
        bool hovered = ImGui::IsMouseHoveringRect(tl, br);
        if (hovered && !sgo->m_wasHovered && sgo->Hovered)
            sgo->Hovered->fire();
        sgo->m_wasHovered = hovered;
    }
}

// ===================================================
//  SurfaceGui のキャンバス→FBO ピクセル座標レイアウト計算
//  ベイク処理(bakeSurfaceGui)とクリックヒットテストの両方で使う共通計算
// ===================================================
struct SurfaceGuiLayout {
    float cW = 0.f, cH = 0.f;         // キャンバスサイズ (SurfaceGui->Size)
    int   w = 0, h = 0;               // FBO ピクセルサイズ
    float scale = 1.f, offX = 0.f, offY = 0.f; // キャンバス→FBOのレターボックス変換
};

static bool computeSurfaceGuiLayout(SurfaceGui* sg, SurfaceGuiLayout& out) {
    out.cW = sg->Size.x; out.cH = sg->Size.y;
    if (out.cW <= 0 || out.cH <= 0) return false;

    // 親 BaseCube からフェイスの物理サイズ（スタッド）を取得
    float faceU = out.cW, faceV = out.cH;
    if (auto par = sg->Parent.lock()) {
        if (par->IsA("BaseCube")) {
            auto* cube = static_cast<BaseCube*>(par.get());
            switch (sg->face) {
                case Face::Front: case Face::Back:
                    faceU = cube->Size.x; faceV = cube->Size.y; break;
                case Face::Top:   case Face::Bottom:
                    faceU = cube->Size.x; faceV = cube->Size.z; break;
                case Face::Right: case Face::Left:
                    faceU = cube->Size.z; faceV = cube->Size.y; break;
            }
        }
    }
    if (faceU <= 0) faceU = out.cW;
    if (faceV <= 0) faceV = out.cH;

    // FBO サイズ = フェイスのアスペクト比に合わせる（幅は cW を基準）
    out.w = (int)out.cW;
    out.h = (int)(out.cW * faceV / faceU);
    if (out.w <= 0 || out.h <= 0) return false;

    // キャンバスを FBO に収めるための均一スケールとオフセット（レターボックス）
    out.scale = (std::min)((float)out.w / out.cW, (float)out.h / out.cH);
    out.offX  = ((float)out.w - out.cW * out.scale) * 0.5f;
    out.offY  = ((float)out.h - out.cH * out.scale) * 0.5f;
    return true;
}

// ===================================================
//  SurfaceGui → FBO テクスチャへのベイク
//  ImGui フレーム内（NewFrame〜EndFrame）で呼ぶこと
// ===================================================
void Renderer::bakeSurfaceGui(SurfaceGui* sg) {
    SurfaceGuiLayout L;
    if (!computeSurfaceGuiLayout(sg, L)) return;
    float cW = L.cW, cH = L.cH;
    int   w  = L.w,  h  = L.h;
    float scale = L.scale, offX = L.offX, offY = L.offY;

    // FBO / テクスチャの作成・リサイズ
    if (sg->m_texID == 0 || sg->m_texW != w || sg->m_texH != h) {
        if (sg->m_fboID) { glDeleteFramebuffers(1, &sg->m_fboID); sg->m_fboID = 0; }
        if (sg->m_texID) { glDeleteTextures(1,    &sg->m_texID);  sg->m_texID = 0; }

        glGenTextures(1, &sg->m_texID);
        glBindTexture(GL_TEXTURE_2D, sg->m_texID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &sg->m_fboID);
        glBindFramebuffer(GL_FRAMEBUFFER, sg->m_fboID);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, sg->m_texID, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        sg->m_texW = w; sg->m_texH = h;
    }

    // GL 状態を保存
    GLint prevFBO; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint vp[4];   glGetIntegerv(GL_VIEWPORT, vp);
    GLfloat prevClearColor[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);

    glBindFramebuffer(GL_FRAMEBUFFER, sg->m_fboID);
    glViewport(0, 0, w, h);
    const Color4& bg = sg->BackgroundColor;
    glClearColor(bg.r, bg.g, bg.b, bg.a);
    glClear(GL_COLOR_BUFFER_BIT);

    // スクリーンパイプラインに乗らない独立リスト（GetDrawListSharedDataを渡せばアクセス違反なし）
    ImDrawList _ownDl(ImGui::GetDrawListSharedData());
    _ownDl._ResetForNewFrame();
    ImDrawList* dl = &_ownDl;
    dl->PushClipRect(ImVec2(0.f, 0.f), ImVec2((float)w, (float)h), false);
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);

    for (auto& [name, child] : sg->getChildren()) {
        if (!child->IsA("ScreenGuiObject")) continue;
        auto* sgo = static_cast<ScreenGuiObject*>(child.get());
        if (!sgo->Visible) continue;

        // キャンバス座標 → FBO 座標（均一スケール + オフセット）
        float cx = (sgo->NormType == Norm::Scale) ? sgo->Position.x * cW : sgo->Position.x;
        float cy = (sgo->NormType == Norm::Scale) ? sgo->Position.y * cH : sgo->Position.y;
        float csw = (sgo->NormType == Norm::Scale) ? sgo->Size.x * cW : sgo->Size.x;
        float csh = (sgo->NormType == Norm::Scale) ? sgo->Size.y * cH : sgo->Size.y;

        float px = offX + cx  * scale;
        float py = offY + cy  * scale;
        float sw = csw * scale;
        float sh = csh * scale;

        drawGuiContent(dl, sgo, px, py, sw, sh, nullptr);
    }

    dl->PopTextureID();
    dl->PopClipRect();

    // 頂点がある場合のみ FBO にレンダリング
    if (dl->VtxBuffer.Size > 0) {
        ImDrawData dd{};
        dd.Valid            = true;
        dd.CmdListsCount    = 1;
        dd.TotalIdxCount    = dl->IdxBuffer.Size;
        dd.TotalVtxCount    = dl->VtxBuffer.Size;
        dd.DisplayPos       = ImVec2(0.f, 0.f);
        dd.DisplaySize      = ImVec2((float)w, (float)h);
        dd.FramebufferScale = ImVec2(1.f, 1.f);
        dd.CmdLists.push_back(dl);
        ImGui_ImplOpenGL3_RenderDrawData(&dd);
    }

    // GL 状態を復元
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
}

// ===================================================
//  renderScreenGui
// ===================================================
void Renderer::renderScreenGui(Workspace& ws, float vpX, float vpY, float vpW, float vpH) {
    std::vector<ScreenGuiObject*> elements;
    collectScreenGui(&ws, elements);
    if (elements.empty()) return;

    // Norm::Pixel 要素のスケール係数: System.BaseResolution に対する現在のビューポート比率
    // System が見つからない/BaseResolutionが不正な場合は 1.0（従来どおりピクセル等倍）にフォールバック
    float scaleX = 1.f, scaleY = 1.f;
    if (auto parent = ws.Parent.lock(); parent && parent->IsA("System")) {
        auto* sys = static_cast<System*>(parent.get());
        if (sys->BaseResolution.x > 0.f) scaleX = vpW / sys->BaseResolution.x;
        if (sys->BaseResolution.y > 0.f) scaleY = vpH / sys->BaseResolution.y;
    }

    // ZIndex 昇順（小 = 背面）
    std::sort(elements.begin(), elements.end(), [](ScreenGuiObject* a, ScreenGuiObject* b) {
        return a->ZIndex < b->ZIndex;
    });

    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (auto* sgo : elements) {
        drawScreenGuiElement(dl, sgo, vpX, vpY, vpW, vpH, scaleX, scaleY, m_onButtonActivated);
    }
}

// ===================================================
//  ワールド→スクリーン投影
// ===================================================
static bool worldToScreen(const Matrix4& view, const Matrix4& proj,
                           float wx, float wy, float wz,
                           float vpX, float vpY, float vpW, float vpH,
                           float& outX, float& outY) {
    // view * [wx,wy,wz,1]  (column-major)
    float vx = view.m[0]*wx + view.m[4]*wy + view.m[8]*wz  + view.m[12];
    float vy = view.m[1]*wx + view.m[5]*wy + view.m[9]*wz  + view.m[13];
    float vz = view.m[2]*wx + view.m[6]*wy + view.m[10]*wz + view.m[14];
    float vw = view.m[3]*wx + view.m[7]*wy + view.m[11]*wz + view.m[15];

    // proj * [vx,vy,vz,vw]
    float cx = proj.m[0]*vx + proj.m[4]*vy + proj.m[8]*vz  + proj.m[12]*vw;
    float cy = proj.m[1]*vx + proj.m[5]*vy + proj.m[9]*vz  + proj.m[13]*vw;
    float cw = proj.m[3]*vx + proj.m[7]*vy + proj.m[11]*vz + proj.m[15]*vw;

    if (cw <= 0.001f) return false;

    float ndcX =  cx / cw;
    float ndcY =  cy / cw;
    outX = (ndcX + 1.0f) * 0.5f * vpW + vpX;
    outY = (1.0f - ndcY) * 0.5f * vpH + vpY;
    return true;
}

// ===================================================
//  WorldGuiObject の子 ScreenGuiObject を描画
// ===================================================
static void drawWorldGuiChildren(ImDrawList* dl, WorldGuiObject* wgo,
                                  float panelX, float panelY, float panelW, float panelH,
                                  std::function<void(GuiButton*)>& onActivated) {
    for (auto& [name, child] : wgo->getChildren()) {
        if (!child->IsA("ScreenGuiObject")) continue;
        auto* sgo = static_cast<ScreenGuiObject*>(child.get());
        // Norm は WorldGui パネル内の相対座標として扱う
        float origNormX = sgo->Position.x, origNormY = sgo->Position.y;
        float origSizeX = sgo->Size.x,     origSizeY = sgo->Size.y;
        Norm origNorm = sgo->NormType;

        // パネル座標にオーバーライドして描画
        float px = (origNorm == Norm::Scale) ? origNormX * panelW + panelX : origNormX + panelX;
        float py = (origNorm == Norm::Scale) ? origNormY * panelH + panelY : origNormY + panelY;
        float sw = (origNorm == Norm::Scale) ? origSizeX * panelW : origSizeX;
        float sh = (origNorm == Norm::Scale) ? origSizeY * panelH : origSizeY;

        drawGuiContent(dl, sgo, px, py, sw, sh, &onActivated);
    }
}

// ===================================================
//  SurfaceGui クリックヒットテスト用のマウスレイ生成
//  ViewportPanel.cpp の makeRay と同じ数式（45°FOV固定、forward/right/up基底）
// ===================================================
static Vector3 makeGuiRay(User* user, float mx, float my, float vpW, float vpH) {
    float ndcX  = (vpW > 0.f) ? (mx / vpW) * 2.0f - 1.0f : 0.0f;
    float ndcY  = (vpH > 0.f) ? 1.0f - (my / vpH) * 2.0f : 0.0f;
    float aspect = (vpW > 0.f && vpH > 0.f) ? vpW / vpH : 1.0f;
    float tanH  = std::tan(45.0f * (3.14159265f / 180.0f) * 0.5f);
    return (user->forward
          + user->right * (ndcX * aspect * tanH)
          + user->up    * (ndcY * tanH)).normalize();
}

// ===================================================
//  レイ vs SurfaceGui 面プレーンの交差判定 → キャンバス座標へ変換して子を当たり判定
//  cube の回転は考慮しない（renderWorldGui の既存の面ワールド座標計算と同じ前提）
// ===================================================
static bool hitTestSurfaceGui(SurfaceGui* sg, BaseCube* cube, const Vector3& rayOri, const Vector3& rayDir,
                               float& outT, ScreenGuiObject*& outChild) {
    float hx = cube->Size.x * 0.5f, hy = cube->Size.y * 0.5f, hz = cube->Size.z * 0.5f;
    CFrame worldCFrame = cube->getWorldCFrame();
    Quaternion inverseRotation = worldCFrame.Rotation.conjugate();
    Vector3 localRayOri = inverseRotation.rotate(rayOri - worldCFrame.Position);
    Vector3 localRayDir = inverseRotation.rotate(rayDir);
    Vector3 planePoint;
    Vector3 normal;
    switch (sg->face) {
        case Face::Front:  planePoint = Vector3(0, 0, hz);  normal = Vector3(0, 0, 1);  break;
        case Face::Back:   planePoint = Vector3(0, 0, -hz); normal = Vector3(0, 0, -1); break;
        case Face::Top:    planePoint = Vector3(0, hy, 0);  normal = Vector3(0, 1, 0);  break;
        case Face::Bottom: planePoint = Vector3(0, -hy, 0); normal = Vector3(0, -1, 0); break;
        case Face::Right:  planePoint = Vector3(hx, 0, 0);  normal = Vector3(1, 0, 0);  break;
        case Face::Left:   planePoint = Vector3(-hx, 0, 0); normal = Vector3(-1, 0, 0); break;
    }

    float denom = Vector3::Dot(localRayDir, normal);
    if (std::abs(denom) < 1e-6f) return false;
    float t = Vector3::Dot(planePoint - localRayOri, normal) / denom;
    if (t < 0.0f) return false;

    Vector3 hitLocal = localRayOri + localRayDir * t;
    if (cube->Size.x == 0.f || cube->Size.y == 0.f || cube->Size.z == 0.f) return false;
    Vector3 localUnit = hitLocal / cube->Size;

    Vector3 uAxis, vAxis;
    switch (sg->face) {
        case Face::Top:    uAxis = Vector3(-1, 0, 0); vAxis = Vector3(0, 0, 1);  break;
        case Face::Bottom: uAxis = Vector3(-1, 0, 0); vAxis = Vector3(0, 0, -1); break;
        case Face::Front:  uAxis = Vector3(-1, 0, 0); vAxis = Vector3(0, 1, 0);  break;
        case Face::Back:   uAxis = Vector3(1, 0, 0);  vAxis = Vector3(0, 1, 0);  break;
        case Face::Right:  uAxis = Vector3(0, 0, -1); vAxis = Vector3(0, 1, 0);  break;
        case Face::Left:   uAxis = Vector3(0, 0, 1);  vAxis = Vector3(0, 1, 0);  break;
    }
    float texU = Vector3::Dot(localUnit, uAxis) + 0.5f;
    float texV = Vector3::Dot(localUnit, vAxis) + 0.5f;
    if (texU < 0.0f || texU > 1.0f || texV < 0.0f || texV > 1.0f) return false;

    SurfaceGuiLayout L;
    if (!computeSurfaceGuiLayout(sg, L)) return false;

    float fboX = texU * (float)L.w;
    // OpenGL の FBO 読み書き(glGetTexImage/テクスチャサンプリング)は行が下から0番になる規約のため、
    // ImGuiの上原点(canvasY=0が上端)の座標系に戻すには Y もミラーする必要がある
    float fboY = (1.0f - texV) * (float)L.h;
    float canvasX = (fboX - L.offX) / L.scale;
    float canvasY = (fboY - L.offY) / L.scale;

    for (auto& [name, child] : sg->getChildren()) {
        if (!child->IsA("ScreenGuiObject")) continue;
        auto* sgo = static_cast<ScreenGuiObject*>(child.get());
        if (!sgo->Visible || !sgo->Active) continue;
        if (!sgo->IsA("GuiButton")) continue;

        float cx  = (sgo->NormType == Norm::Scale) ? sgo->Position.x * L.cW : sgo->Position.x;
        float cy  = (sgo->NormType == Norm::Scale) ? sgo->Position.y * L.cH : sgo->Position.y;
        float csw = (sgo->NormType == Norm::Scale) ? sgo->Size.x * L.cW : sgo->Size.x;
        float csh = (sgo->NormType == Norm::Scale) ? sgo->Size.y * L.cH : sgo->Size.y;

        if (canvasX >= cx && canvasX <= cx + csw && canvasY >= cy && canvasY <= cy + csh) {
            outT = t;
            outChild = sgo;
            return true;
        }
    }
    return false;
}

// ===================================================
//  renderWorldGui
// ===================================================
void Renderer::renderWorldGui(Workspace& ws, User* user, float vpX, float vpY, float vpW, float vpH) {
    std::vector<BaseCube*> guiHosts;
    collectWorldGuiHosts(&ws, guiHosts);

    // SurfaceGui を FBO テクスチャにベイク（次フレームの 3D 描画で使用）
    for (BaseCube* cube : guiHosts) {
        for (auto& [gname, ginst] : cube->getChildren()) {
            if (ginst->getClassName() == "SurfaceGui")
                bakeSurfaceGui(static_cast<SurfaceGui*>(ginst.get()));
        }
    }

    // SurfaceGui クリック判定: 全キューブの全 SurfaceGui からレイと最も近く交差したボタンを探して発火する
    if (user && ImGui::IsMouseClicked(0)) {
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector3 rayOri = user->cpos;
        Vector3 rayDir = makeGuiRay(user, mousePos.x - vpX, mousePos.y - vpY, vpW, vpH);

        float bestT = 1e30f;
        GuiButton* bestBtn = nullptr;
        for (BaseCube* cube : guiHosts) {
            for (auto& [gname, ginst] : cube->getChildren()) {
                if (ginst->getClassName() != "SurfaceGui") continue;
                auto* sg = static_cast<SurfaceGui*>(ginst.get());
                if (!sg->Visible) continue;

                float t; ScreenGuiObject* hitChild = nullptr;
                if (hitTestSurfaceGui(sg, cube, rayOri, rayDir, t, hitChild) && t < bestT) {
                    bestT   = t;
                    bestBtn = static_cast<GuiButton*>(hitChild);
                }
            }
        }
        if (bestBtn && m_onButtonActivated) m_onButtonActivated(bestBtn);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool anyPromptHeld = false;

    for (BaseCube* cube : guiHosts) {

        for (auto& [guiName, guiInst] : cube->getChildren()) {
            if (!guiInst->IsA("WorldGuiObject")) continue;
            auto* wgo = static_cast<WorldGuiObject*>(guiInst.get());
            if (!wgo->Visible) continue;
            if (wgo->getClassName() == "SurfaceGui") continue; // 3D フェイス描画に移行

            CFrame cubeWorldCFrame = cube->getWorldCFrame();
            Vector3 guiCenterOffset(0.0f, 0.0f, 0.0f);

            // SurfaceGui: フェイス中心にオフセット
            if (wgo->IsA("SurfaceGui")) {
                auto* sg = static_cast<SurfaceGui*>(wgo);
                float hx = cube->Size.x * 0.5f;
                float hy = cube->Size.y * 0.5f;
                float hz = cube->Size.z * 0.5f;
                switch (sg->face) {
                    case Face::Front:  guiCenterOffset.z += hz; break;
                    case Face::Back:   guiCenterOffset.z -= hz; break;
                    case Face::Top:    guiCenterOffset.y += hy; break;
                    case Face::Bottom: guiCenterOffset.y -= hy; break;
                    case Face::Right:  guiCenterOffset.x += hx; break;
                    case Face::Left:   guiCenterOffset.x -= hx; break;
                }
            }

            Vector3 guiCenter = cubeWorldCFrame.pointToWorld(guiCenterOffset);

            bool isProximityPrompt = (wgo->getClassName() == "ProximityPrompt");
            ProximityPrompt* pp = isProximityPrompt ? static_cast<ProximityPrompt*>(wgo) : nullptr;

            if (pp) {
                if (!pp->Enabled) continue;
                if (!SystemState::get().isPlaying) continue;

                // 距離チェック
                User* localUser = User::getInstance();
                if (!localUser || !localUser->humanoid || !localUser->humanoid->Root) continue;

                Vector3 playerPos = localUser->humanoid->getRootWorldPosition();
                Vector3 cubePos = guiCenter;
                float dist = (playerPos - cubePos).length();
                if (dist > pp->MaxActivationDistance) {
                    pp->m_elapsedTime = 0.0f;
                    pp->m_isHolding = false;
                    pp->m_hasTriggered = false;
                    continue;
                }

                // キー入力判定と状態更新
                double curTime = glfwGetTime();
                if (pp->m_lastUpdateTime == 0.0) pp->m_lastUpdateTime = curTime;
                float dt = (float)(curTime - pp->m_lastUpdateTime);
                pp->m_lastUpdateTime = curTime;

                auto getGlfwKey = [](const std::string& keyStr) -> int {
                    std::string s = keyStr;
                    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
                    if (s.length() == 1 && s[0] >= 'A' && s[0] <= 'Z') return GLFW_KEY_A + (s[0] - 'A');
                    if (s.length() == 1 && s[0] >= '0' && s[0] <= '9') return GLFW_KEY_0 + (s[0] - '0');
                    if (s == "SPACE") return GLFW_KEY_SPACE;
                    if (s == "ENTER") return GLFW_KEY_ENTER;
                    if (s == "SHIFT" || s == "LEFT_SHIFT") return GLFW_KEY_LEFT_SHIFT;
                    if (s == "CTRL" || s == "LEFT_CONTROL" || s == "CONTROL") return GLFW_KEY_LEFT_CONTROL;
                    if (s == "ALT" || s == "LEFT_ALT") return GLFW_KEY_LEFT_ALT;
                    return GLFW_KEY_E;
                };

                int glfwKey = getGlfwKey(pp->KeyboardKeyCode);
                GLFWwindow* window = Renderer::instance->m_window;
                bool isKeyPressed = false;
                if (window && SystemState::get().viewportFocused) {
                    isKeyPressed = (glfwGetKey(window, glfwKey) == GLFW_PRESS);
                }

                if (isKeyPressed) {
                    pp->m_isHolding = true;
                    anyPromptHeld = true;
                    if (!pp->m_hasTriggered) {
                        pp->m_elapsedTime += dt;
                        if (pp->HoldDuration <= 0.0f) {
                            pp->m_elapsedTime = 0.0f;
                            pp->m_hasTriggered = true;
                            if (pp->Triggered) pp->Triggered->fire();
                        } else if (pp->m_elapsedTime >= pp->HoldDuration) {
                            pp->m_elapsedTime = pp->HoldDuration;
                            pp->m_hasTriggered = true;
                            if (pp->Triggered) pp->Triggered->fire();
                        }
                    }
                } else {
                    pp->m_isHolding = false;
                    pp->m_hasTriggered = false;
                    pp->m_elapsedTime = 0.0f;
                }
            }

            float sx, sy;
            if (!worldToScreen(m_lastView, m_lastProj, guiCenter.x, guiCenter.y, guiCenter.z,
                                vpX, vpY, vpW, vpH, sx, sy)) continue;

            // パネル描画: スクリーン座標 (sx,sy) を中心にサイズを配置
            float pw = wgo->Size.x, ph = wgo->Size.y;
            float panelX = sx - pw * 0.5f;
            float panelY = sy - ph * 0.5f;

            if (pp) {
                const Color4& bg = pp->BackgroundColor;
                ImU32 bgCol = IM_COL32((int)(bg.r*255),(int)(bg.g*255),(int)(bg.b*255),(int)(bg.a*255));
                dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), bgCol, 8.0f);
                dl->AddRect(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), IM_COL32(255,255,255,80), 8.0f, 0, 1.5f);

                float textY = panelY + 8.0f;
                if (!pp->ObjectText.empty()) {
                    ImVec2 textSize = ImGui::CalcTextSize(pp->ObjectText.c_str());
                    dl->AddText(ImVec2(panelX + (pw - textSize.x)*0.5f, textY), IM_COL32(200,200,200,255), pp->ObjectText.c_str());
                    textY += textSize.y + 4.0f;
                }

                std::string actionStr = "[" + pp->KeyboardKeyCode + "] " + pp->ActionText;
                ImVec2 actionSize = ImGui::CalcTextSize(actionStr.c_str());
                dl->AddText(ImVec2(panelX + (pw - actionSize.x)*0.5f, textY), IM_COL32(255,255,255,255), actionStr.c_str());

                if (pp->HoldDuration > 0.0f) {
                    float progress = pp->m_elapsedTime / pp->HoldDuration;
                    if (progress > 1.0f) progress = 1.0f;

                    float barMargin = 12.0f;
                    float barY = panelY + ph - 12.0f;
                    float barW = pw - barMargin * 2.0f;
                    float barH = 5.0f;
                    dl->AddRectFilled(ImVec2(panelX + barMargin, barY), ImVec2(panelX + barMargin + barW, barY + barH), IM_COL32(50,50,50,255), 2.0f);
                    if (progress > 0.0f) {
                        dl->AddRectFilled(ImVec2(panelX + barMargin, barY), ImVec2(panelX + barMargin + barW * progress, barY + barH), IM_COL32(100,255,100,255), 2.0f);
                    }
                }

                drawWorldGuiChildren(dl, wgo, panelX, panelY, pw, ph, m_onButtonActivated);
                continue;
            }

            const Color4& bg = wgo->BackgroundColor;
            ImU32 bgCol = IM_COL32((int)(bg.r*255),(int)(bg.g*255),(int)(bg.b*255),(int)(bg.a*255));
            dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), bgCol);

            drawWorldGuiChildren(dl, wgo, panelX, panelY, pw, ph, m_onButtonActivated);
        }
    }

    if (SystemState::get().isPlaying) {
        SystemState::get().inputState = anyPromptHeld
            ? InputState::ProximityPrompt
            : InputState::Gameplay;
    }
}

// ===================================================
//  renderToolHotbar
// ===================================================
void Renderer::renderToolHotbar(User& user, float vpX, float vpY, float vpW, float vpH) {
    if (!user.currentTool) return;

    struct SlotEntry { int index; Tool* tool; };
    std::vector<SlotEntry> visible;
    for (int i = 0; i < 10; i++) {
        if (user.Slots[i]) visible.push_back({ i, user.Slots[i].get() });
    }
    if (visible.empty()) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float boxW = 56.0f, boxH = 56.0f, spacing = 8.0f, bottomMargin = 20.0f;
    float totalW = visible.size() * boxW + (visible.size() - 1) * spacing;
    float startX = vpX + (vpW - totalW) * 0.5f;
    float boxY   = vpY + vpH - boxH - bottomMargin;

    for (size_t n = 0; n < visible.size(); n++) {
        const SlotEntry& entry = visible[n];
        float boxX = startX + n * (boxW + spacing);
        bool isEquipped = (entry.index == user.currentSlotIndex);

        ImU32 bgCol = isEquipped ? IM_COL32(60, 60, 30, 220) : IM_COL32(20, 20, 20, 180);
        dl->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), bgCol, 6.0f);

        ImU32 borderCol = isEquipped ? IM_COL32(255, 220, 80, 255) : IM_COL32(255, 255, 255, 80);
        float borderThickness = isEquipped ? 2.5f : 1.5f;
        dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), borderCol, 6.0f, 0, borderThickness);

        std::string keyLabel = (entry.index == 9) ? "0" : std::to_string(entry.index + 1);
        ImVec2 keySize = ImGui::CalcTextSize(keyLabel.c_str());
        dl->AddText(ImVec2(boxX + (boxW - keySize.x) * 0.5f, boxY + 4.0f), IM_COL32(200, 200, 200, 255), keyLabel.c_str());

        const std::string& nameLabel = entry.tool->Name;
        ImVec2 nameSize = ImGui::CalcTextSize(nameLabel.c_str());
        dl->AddText(ImVec2(boxX + (boxW - nameSize.x) * 0.5f, boxY + boxH - nameSize.y - 4.0f), IM_COL32(255, 255, 255, 255), nameLabel.c_str());
    }
}

// ===================================================
//  renderGameGui — ScreenGui + WorldGui + ToolHotbar の統合描画
// ===================================================
void Renderer::renderGameGui(Workspace& ws, User* user, float vpX, float vpY, float vpW, float vpH) {
    if (user) user->setGameViewport(vpX, vpY, vpW, vpH);
    renderScreenGui(ws, vpX, vpY, vpW, vpH);
    renderWorldGui (ws, user, vpX, vpY, vpW, vpH);
    if (user) renderToolHotbar(*user, vpX, vpY, vpW, vpH);
}

// ===================================================
//  カメラ回転ドラッグ中の擬似カーソル
//
//  OSカーソルはRaw Mouse Motionのために非表示のままにする(setMouseCaptured/
//  GLFW_CURSOR_DISABLEDは変更しない)。代わりにアンカー位置(ドラッグ開始位置)へ
//  assets/image/cursor.svg の矢印形状をImGuiの図形描画でクローンして毎フレーム
//  固定表示することで、見た目上カーソルが消えたままにならないようにする。
// ===================================================
void Renderer::drawCameraRotationCursor(User& user, GLFWwindow* window) {
    if (!user.isRotatingCamera()) return;

    double ax = 0.0, ay = 0.0;
    user.getRotationAnchor(ax, ay);

    // getRotationAnchorはウィンドウクライアント座標。マルチビューポート有効時、
    // ImGuiのスクリーン座標はデスクトップ絶対座標になるためウィンドウ位置を加算する
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        int wx = 0, wy = 0;
        glfwGetWindowPos(window, &wx, &wy);
        ax += wx;
        ay += wy;
    }

    // cursor.svg (矢印の先端をホットスポット(0,0)としたローカル座標に変換済み) の
    // 2つのパス: 本体の三角形と、右下の小さな尾の四角形
    const ImVec2 body[3] = {
        ImVec2(static_cast<float>(ax) + 0.0f,     static_cast<float>(ay) + 0.0f),
        ImVec2(static_cast<float>(ax) + 0.0f,     static_cast<float>(ay) + 17.108f),
        ImVec2(static_cast<float>(ax) + 11.762f,  static_cast<float>(ay) + 12.000f),
    };
    const ImVec2 tail[4] = {
        ImVec2(static_cast<float>(ax) + 4.039f,   static_cast<float>(ay) + 14.257f),
        ImVec2(static_cast<float>(ax) + 7.247f,   static_cast<float>(ay) + 13.188f),
        ImVec2(static_cast<float>(ax) + 10.455f,  static_cast<float>(ay) + 19.603f),
        ImVec2(static_cast<float>(ax) + 7.247f,   static_cast<float>(ay) + 21.029f),
    };

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 fillColor   = IM_COL32(255, 255, 255, 255);
    const ImU32 strokeColor = IM_COL32(0, 0, 0, 255);

    dl->AddConvexPolyFilled(body, 3, fillColor);
    dl->AddConvexPolyFilled(tail, 4, fillColor);
    dl->AddPolyline(body, 3, strokeColor, ImDrawFlags_Closed, 2.0f);
    dl->AddPolyline(tail, 4, strokeColor, ImDrawFlags_Closed, 2.0f);
}
