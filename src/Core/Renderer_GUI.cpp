#include "include/Core/Renderer.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/TextLabel.hpp"
#include "include/Instances/GuiButton.hpp"
#include "include/Instances/TextButton.hpp"
#include "include/Instances/WorldGuiObject.hpp"
#include "include/Instances/SurfaceGui.hpp"
#include "include/Instances/BillboardGui.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/imgui/imgui.h"
#include "include/imgui/imgui_impl_opengl3.h"

#include <algorithm>
#include <vector>

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

// ===================================================
//  ScreenGui 1 要素の描画
// ===================================================
static void drawScreenGuiElement(ImDrawList* dl, ScreenGuiObject* sgo,
                                  float vpX, float vpY, float vpW, float vpH,
                                  std::function<void(GuiButton*)>& onActivated) {
    if (!sgo->Visible) return;

    float px = (sgo->NormType == Norm::Scale) ? sgo->Position.x * vpW + vpX : sgo->Position.x + vpX;
    float py = (sgo->NormType == Norm::Scale) ? sgo->Position.y * vpH + vpY : sgo->Position.y + vpY;
    float sw = (sgo->NormType == Norm::Scale) ? sgo->Size.x * vpW : sgo->Size.x;
    float sh = (sgo->NormType == Norm::Scale) ? sgo->Size.y * vpH : sgo->Size.y;

    ImVec2 tl(px, py);
    ImVec2 br(px + sw, py + sh);

    const Color4& bg = sgo->BackgroundColor;
    ImU32 bgCol = IM_COL32((int)(bg.r * 255), (int)(bg.g * 255),
                            (int)(bg.b * 255), (int)(bg.a * 255));

    // バックグラウンド
    dl->AddRectFilled(tl, br, bgCol);

    if (sgo->IsA("TextButton")) {
        auto* btn = static_cast<TextButton*>(sgo);
        const Color4& tc = btn->TextColor;
        ImU32 textCol = IM_COL32((int)(tc.r*255),(int)(tc.g*255),(int)(tc.b*255),(int)(tc.a*255));
        if (!btn->Text.empty())
            dl->AddText(ImVec2(px + 4, py + (sh - ImGui::GetTextLineHeight()) * 0.5f),
                        textCol, btn->Text.c_str());

        // ヒットテスト用 InvisibleButton
        if (sgo->Active) {
            ImGui::SetCursorScreenPos(tl);
            std::string btnId = "##btn_" + sgo->Name;
            ImGui::InvisibleButton(btnId.c_str(), ImVec2(sw, sh));
            if (ImGui::IsItemClicked() && onActivated) {
                onActivated(btn);
            }
        }
    } else if (sgo->IsA("TextLabel")) {
        auto* lbl = static_cast<TextLabel*>(sgo);
        const Color4& tc = lbl->TextColor;
        ImU32 textCol = IM_COL32((int)(tc.r*255),(int)(tc.g*255),(int)(tc.b*255),(int)(tc.a*255));
        if (!lbl->Text.empty())
            dl->AddText(ImVec2(px + 4, py + (sh - ImGui::GetTextLineHeight()) * 0.5f),
                        textCol, lbl->Text.c_str());
    }
}

// ===================================================
//  SurfaceGui → FBO テクスチャへのベイク
//  ImGui フレーム内（NewFrame〜EndFrame）で呼ぶこと
// ===================================================
void Renderer::bakeSurfaceGui(SurfaceGui* sg) {
    // printf("\033[46m Baking SurfaceGui '%s' (texID=%u, size=%.0fx%.0f)\033[0m\n",
    //     sg->Name.c_str(), sg->m_texID, sg->Size.x, sg->Size.y);

    int w = (int)sg->Size.x, h = (int)sg->Size.y;
    if (w <= 0 || h <= 0) return;

    // FBO / テクスチャの作成・リサイズ
    // printf("\033[46m Creating FBO and texture... \033[0m\n");
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
    // printf("\033[46m Created FBO and texture! \033[0m\n");
    // GL 状態を保存
    GLint prevFBO; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint vp[4];   glGetIntegerv(GL_VIEWPORT, vp);

    // printf("\033[46m Setting FrameBuffer... \033[0m\n");
    glBindFramebuffer(GL_FRAMEBUFFER, sg->m_fboID);
    glViewport(0, 0, w, h);
    const Color4& bg = sg->BackgroundColor;
    glClearColor(bg.r, bg.g, bg.b, bg.a);
    glClear(GL_COLOR_BUFFER_BIT);
    // printf("\033[46m FrameBuffer Done! \033[0m\n");


    // 子要素をビジュアルのみ描画（InvisibleButton など ImGui ウィンドウ操作は行わない）
    // printf("\033[46m trying to draw children... \033[0m\n");

    /*!!!*/ImDrawList* dl = ImGui::GetWindowDrawList(); // !!! IMPORTANT: これ以外でdlを作るとアクセス違反が発生し、まともに動作しない
    dl->PushClipRect(ImVec2(0.f, 0.f), ImVec2((float)w, (float)h), false);
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);

    float lineH = ImGui::GetTextLineHeight();
    for (auto& [name, child] : sg->getChildren()) {
        if (!child->IsA("ScreenGuiObject")) continue;
        auto* sgo = static_cast<ScreenGuiObject*>(child.get());
        if (!sgo->Visible) continue;

        float px = (sgo->NormType == Norm::Scale) ? sgo->Position.x * w : sgo->Position.x;
        float py = (sgo->NormType == Norm::Scale) ? sgo->Position.y * h : sgo->Position.y;
        float sw = (sgo->NormType == Norm::Scale) ? sgo->Size.x * w : sgo->Size.x;
        float sh = (sgo->NormType == Norm::Scale) ? sgo->Size.y * h : sgo->Size.y;

        const Color4& bgc = sgo->BackgroundColor;
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + sw, py + sh),
            IM_COL32((int)(bgc.r*255),(int)(bgc.g*255),(int)(bgc.b*255),(int)(bgc.a*255)));

        const char* text = nullptr;
        const Color4* tc = nullptr;
        if (sgo->IsA("TextLabel")) {
            auto* lbl = static_cast<TextLabel*>(sgo);
            if (!lbl->Text.empty()) { text = lbl->Text.c_str(); tc = &lbl->TextColor; }
        } else if (sgo->IsA("TextButton")) {
            auto* btn = static_cast<TextButton*>(sgo);
            if (!btn->Text.empty()) { text = btn->Text.c_str(); tc = &btn->TextColor; }
        }
        if (text && tc)
            dl->AddText(ImVec2(px + 4.f, py + (sh - lineH) * 0.5f),
                IM_COL32((int)(tc->r*255),(int)(tc->g*255),(int)(tc->b*255),(int)(tc->a*255)),
                text);
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

    // printf("\033[46m Finished baking SurfaceGui(pointer: %p)\033[0m\n", sg);
}

// ===================================================
//  renderScreenGui
// ===================================================
void Renderer::renderScreenGui(Workspace& ws, float vpX, float vpY, float vpW, float vpH) {
    std::vector<ScreenGuiObject*> elements;
    collectScreenGui(&ws, elements);
    if (elements.empty()) return;

    // ZIndex 昇順（小 = 背面）
    std::sort(elements.begin(), elements.end(), [](ScreenGuiObject* a, ScreenGuiObject* b) {
        return a->ZIndex < b->ZIndex;
    });

    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (auto* sgo : elements) {
        drawScreenGuiElement(dl, sgo, vpX, vpY, vpW, vpH, m_onButtonActivated);
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

        ImVec2 tl(px, py);
        ImVec2 br(px + sw, py + sh);
        const Color4& bg = sgo->BackgroundColor;
        ImU32 bgCol = IM_COL32((int)(bg.r*255),(int)(bg.g*255),(int)(bg.b*255),(int)(bg.a*255));
        dl->AddRectFilled(tl, br, bgCol);

        if (sgo->IsA("TextLabel")) {
            auto* lbl = static_cast<TextLabel*>(sgo);
            const Color4& tc = lbl->TextColor;
            ImU32 textCol = IM_COL32((int)(tc.r*255),(int)(tc.g*255),(int)(tc.b*255),(int)(tc.a*255));
            if (!lbl->Text.empty())
                dl->AddText(ImVec2(px+4, py+(sh-ImGui::GetTextLineHeight())*0.5f), textCol, lbl->Text.c_str());
        } else if (sgo->IsA("TextButton")) {
            auto* btn = static_cast<TextButton*>(sgo);
            const Color4& tc = btn->TextColor;
            ImU32 textCol = IM_COL32((int)(tc.r*255),(int)(tc.g*255),(int)(tc.b*255),(int)(tc.a*255));
            if (!btn->Text.empty())
                dl->AddText(ImVec2(px+4, py+(sh-ImGui::GetTextLineHeight())*0.5f), textCol, btn->Text.c_str());
            if (sgo->Active) {
                ImGui::SetCursorScreenPos(tl);
                std::string btnId = "##wbtn_" + sgo->Name;
                ImGui::InvisibleButton(btnId.c_str(), ImVec2(sw, sh));
                if (ImGui::IsItemClicked() && onActivated) onActivated(btn);
            }
        }
    }
}

// ===================================================
//  renderWorldGui
// ===================================================
void Renderer::renderWorldGui(Workspace& ws, float vpX, float vpY, float vpW, float vpH) {
    // SurfaceGui を FBO テクスチャにベイク（次フレームの 3D 描画で使用）
    for (auto& [name, inst] : ws.getChildren()) {
        if (!inst->IsA("BaseCube")) continue;
        for (auto& [gname, ginst] : inst->getChildren()) {
            if (ginst->GetClassName() == "SurfaceGui")
                bakeSurfaceGui(static_cast<SurfaceGui*>(ginst.get()));
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (auto& [cubeNameStr, cubeInst] : ws.getChildren()) {
        if (!cubeInst->IsA("BaseCube")) continue;
        auto* cube = static_cast<BaseCube*>(cubeInst.get());

        for (auto& [guiName, guiInst] : cubeInst->getChildren()) {
            if (!guiInst->IsA("WorldGuiObject")) continue;
            auto* wgo = static_cast<WorldGuiObject*>(guiInst.get());
            if (!wgo->Visible) continue;
            if (wgo->GetClassName() == "SurfaceGui") continue; // 3D フェイス描画に移行

            float wx = cube->Position.x;
            float wy = cube->Position.y;
            float wz = cube->Position.z;

            // SurfaceGui: フェイス中心にオフセット
            if (wgo->IsA("SurfaceGui")) {
                auto* sg = static_cast<SurfaceGui*>(wgo);
                float hx = cube->Size.x * 0.5f;
                float hy = cube->Size.y * 0.5f;
                float hz = cube->Size.z * 0.5f;
                switch (sg->face) {
                    case Face::Front:  wz += hz; break;
                    case Face::Back:   wz -= hz; break;
                    case Face::Top:    wy += hy; break;
                    case Face::Bottom: wy -= hy; break;
                    case Face::Right:  wx += hx; break;
                    case Face::Left:   wx -= hx; break;
                }
            }

            float sx, sy;
            if (!worldToScreen(m_lastView, m_lastProj, wx, wy, wz,
                                vpX, vpY, vpW, vpH, sx, sy)) continue;

            // パネル描画: スクリーン座標 (sx,sy) を中心にサイズを配置
            float pw = wgo->Size.x, ph = wgo->Size.y;
            float panelX = sx - pw * 0.5f;
            float panelY = sy - ph * 0.5f;

            const Color4& bg = wgo->BackgroundColor;
            ImU32 bgCol = IM_COL32((int)(bg.r*255),(int)(bg.g*255),(int)(bg.b*255),(int)(bg.a*255));
            dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), bgCol);

            drawWorldGuiChildren(dl, wgo, panelX, panelY, pw, ph, m_onButtonActivated);
        }
    }
}
