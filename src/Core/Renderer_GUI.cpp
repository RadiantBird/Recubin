#include "include/Core/Renderer.hpp"
#include "include/Core/SystemState.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/FontFile.hpp"
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
#include "include/Util/AssetPath.hpp"
#include "include/Util/Logger.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

static Instance* sceneRoot(Instance* node) {
    if (!node) return nullptr;
    while (auto parent = node->Parent.lock()) node = parent.get();
    return node;
}

static std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

static FontFile* findFontFile(Instance* node, const std::string& reference) {
    if (!node) return nullptr;
    const std::string normalizedReference = AssetPath::normalize(reference);
    for (auto& [childName, child] : node->getChildren()) {
        if (child->getClassName() == "FontFile") {
            auto* fontFile = static_cast<FontFile*>(child.get());
            if (AssetPath::normalize(fontFile->getWorkspaceRelativePath()) == normalizedReference ||
                AssetPath::normalize(fontFile->getFullPath()) == normalizedReference ||
                fontFile->Name == reference) {
                return fontFile;
            }
        }
        if (auto* result = findFontFile(child.get(), reference)) return result;
    }
    return nullptr;
}

static std::filesystem::path resolveStoredFontPath(const std::string& storedPath) {
    const std::filesystem::path path = AssetPath::fromStored(storedPath);
    if (path.is_absolute()) return path;

    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    return ec ? path : current / path;
}

// ImGui::AddFontFromFileTTF() はナローパスの fopen() を使うため、Windows では
// UTF-8 を含む FontFile.ContentPath を開けない。filesystem::path 経由で読み込んで
// ImGui に所有権を渡せば、Unicode パスでも同じフォントローダーを安全に使える。
static ImFont* addFontFromPath(const std::filesystem::path& fontPath) {
    std::ifstream input(fontPath, std::ios::binary | std::ios::ate);
    if (!input) return nullptr;

    const std::streamsize size = input.tellg();
    if (size <= 100 || size > std::numeric_limits<int>::max()) return nullptr;

    void* data = IM_ALLOC(static_cast<size_t>(size));
    if (!data) return nullptr;

    input.seekg(0, std::ios::beg);
    if (!input.read(static_cast<char*>(data), size)) {
        IM_FREE(data);
        return nullptr;
    }

    ImFontConfig config;
    config.FontDataOwnedByAtlas = true;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        data, static_cast<int>(size), 22.0f, &config,
        ImGui::GetIO().Fonts->GetGlyphRangesJapanese());
}

ImFont* Renderer::loadGuiFont(ScreenGuiObject* sgo) {
    if (!sgo) return ImGui::GetFont();

    FontFile* fontFile = !sgo->UseFontFile || sgo->FontFile.empty()
        ? nullptr
        : findFontFile(sceneRoot(sgo), sgo->FontFile);
    if (!fontFile || fontFile->Path.empty()) {
        RCBN_WARN("FontFile reference could not be resolved: " << sgo->FontFile);
        return ImGui::GetFont();
    }

    const std::filesystem::path fontPath = resolveStoredFontPath(fontFile->Path);
    std::error_code ec;
    if (!std::filesystem::exists(fontPath, ec) || ec) {
        RCBN_WARN("FontFile TTF/OTF not found: " << fontFile->Path
                  << " (resolved: " << pathToUtf8(fontPath) << ")");
        return ImGui::GetFont();
    }

    const std::wstring cacheKey = fontPath.lexically_normal().wstring();
    auto cached = m_guiFontCache.find(cacheKey);
    if (cached != m_guiFontCache.end()) {
        return cached->second ? cached->second : ImGui::GetFont();
    }

    ImFont* loaded = addFontFromPath(fontPath);
    m_guiFontCache[cacheKey] = loaded;
    if (!loaded) {
        RCBN_WARN("Failed to load FontFile: " << pathToUtf8(fontPath));
        return ImGui::GetFont();
    }
    return loaded;
}

static void collectFontFileUsers(Instance* node, std::vector<ScreenGuiObject*>& out) {
    if (!node) return;
    if (node->IsA("ScreenGuiObject"))
        out.push_back(static_cast<ScreenGuiObject*>(node));
    for (const auto& [name, child] : node->getChildren()) {
        (void)name;
        collectFontFileUsers(child.get(), out);
    }
}

void Renderer::prepareGuiFonts(Workspace& workspace) {
    std::vector<ScreenGuiObject*> guiObjects;
    collectFontFileUsers(sceneRoot(&workspace), guiObjects);
    for (ScreenGuiObject* guiObject : guiObjects) {
        if (guiObject->UseFontFile)
            loadGuiFont(guiObject);
    }
}

ImFont* Renderer::resolveGuiFont(ScreenGuiObject* sgo) {
    if (!sgo) return ImGui::GetFont();

    if (!sgo->UseFontFile) {
        switch (sgo->Font) {
            case SystemFont::DotGothic16:
                return m_dotGothicGuiFont ? m_dotGothicGuiFont : ImGui::GetFont();
            case SystemFont::Default:
            default:
                return m_systemDefaultGuiFont ? m_systemDefaultGuiFont : ImGui::GetFont();
        }
    }

    FontFile* fontFile = sgo->FontFile.empty()
        ? nullptr
        : findFontFile(sceneRoot(sgo), sgo->FontFile);
    if (!fontFile || fontFile->Path.empty()) return ImGui::GetFont();

    const std::filesystem::path fontPath = resolveStoredFontPath(fontFile->Path);
    const std::wstring cacheKey = fontPath.lexically_normal().wstring();
    const auto cached = m_guiFontCache.find(cacheKey);
    return cached != m_guiFontCache.end() && cached->second
        ? cached->second
        : ImGui::GetFont();
}

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
// テキストを要素矩形内の縦中央へ描画。FontSize>0 なら指定サイズ、0 なら既定サイズを使う。
// BillboardGuiの子ではfitToBoundsを有効にし、指定サイズを上限として文字列全体を収める。
static void drawGuiText(ImDrawList* dl, ScreenGuiObject* sgo,
                        float px, float py, float sw, float sh,
                        ImU32 col, const char* text,
                        float textScaleY = 1.0f, bool fitToBounds = false)
{
    if (sw <= 0.0f || sh <= 0.0f) return;

    ImFont* font = Renderer::instance
        ? Renderer::instance->resolveGuiFont(sgo)
        : ImGui::GetFont();

    float size = ((sgo->FontSize > 0.f) ? sgo->FontSize : ImGui::GetFontSize()) * textScaleY;
    if (size <= 0.0f) return;

    // 左右の余白はImGui論理ピクセル。極端に小さいWorldパネルでも負の描画領域にしない。
    const float paddingX = (std::min)(4.0f, sw * 0.5f);
    const float availableWidth = (std::max)(0.0f, sw - paddingX * 2.0f);
    ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);

    if (fitToBounds && textSize.x > 0.0f && textSize.y > 0.0f) {
        const float fitScale = (std::min)({
            1.0f,
            availableWidth / textSize.x,
            sh / textSize.y
        });
        size *= (std::max)(0.0f, fitScale);
        if (size <= 0.0f) return;
        textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    }

    const ImVec2 textPosition(
        px + paddingX,
        py + (std::max)(0.0f, (sh - textSize.y) * 0.5f));
    const ImVec4 textClipRect(px, py, px + sw, py + sh);
    dl->AddText(font, size, textPosition, col, text, nullptr, 0.0f, &textClipRect);
}

static ImU32 toImCol(const Color4& c) {
    return IM_COL32((int)(c.r*255), (int)(c.g*255), (int)(c.b*255), (int)(c.a*255));
}

// 背景+画像+文字+（任意）ボタン当たり判定の共通描画。
// Screen直描画 / SurfaceGuiベイク / BillboardGuiパネル の3系統で共用する。
// onActivated が null のときは非対話（ベイク用: InvisibleButton を発行しない）
static void drawGuiContent(ImDrawList* dl, ScreenGuiObject* sgo,
                           float px, float py, float sw, float sh,
                           std::function<void(GuiButton*)>* onActivated,
                           float textScaleY = 1.0f,
                           bool fitTextToBounds = false) {
    ImVec2 tl(px, py), br(px + sw, py + sh);
    dl->AddRectFilled(tl, br, toImCol(sgo->BackgroundColor));
    if (auto* img = sgo->imageContent(); img && img->textureID != 0)
        dl->AddImage((ImTextureID)(uintptr_t)img->textureID, tl, br,
                     ImVec2(0, 1), ImVec2(1, 0));  // 上下反転（テクスチャ原点補正）
    if (auto* txt = sgo->textContent(); txt && !txt->Text.empty())
        drawGuiText(dl, sgo, px, py, sw, sh, toImCol(txt->TextColor), txt->Text.c_str(),
                    textScaleY, fitTextToBounds);
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
        if (!hovered && sgo->m_wasHovered && sgo->IsA("GuiButton"))
            static_cast<GuiButton*>(sgo)->HoverEnded->fire();
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

struct BillboardLayout {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// BillboardGuiのWorldモード用レイアウト。
// Sizeのワールド幅・高さをカメラ平面のright/up方向へ展開して投影し、
// 画面上のパネルサイズと、子GUIへ渡すワールド→ピクセル倍率を求める。
static bool computeBillboardLayout(const Matrix4& view, const Matrix4& proj,
                                   const BillboardGui& billboard, const Vector3& center,
                                   float vpX, float vpY, float vpW, float vpH,
                                   BillboardLayout& out) {
    if (billboard.Size.x <= 0.0f || billboard.Size.y <= 0.0f) return false;

    if (!worldToScreen(view, proj, center.x, center.y, center.z,
                       vpX, vpY, vpW, vpH, out.centerX, out.centerY)) {
        return false;
    }

    // view行列の1行目/2行目はワールド座標をカメラright/upへ変換する係数。
    // 軸ベクトルを取り出して、カメラ平面上のワールド矩形を作る。
    Vector3 cameraRight(view.m[0], view.m[4], view.m[8]);
    Vector3 cameraUp(view.m[1], view.m[5], view.m[9]);
    cameraRight = cameraRight.normalize();
    cameraUp = cameraUp.normalize();

    float leftX, leftY, rightX, rightY;
    Vector3 left = center - cameraRight * (billboard.Size.x * 0.5f);
    Vector3 right = center + cameraRight * (billboard.Size.x * 0.5f);
    if (!worldToScreen(view, proj, left.x, left.y, left.z,
                       vpX, vpY, vpW, vpH, leftX, leftY)
        || !worldToScreen(view, proj, right.x, right.y, right.z,
                          vpX, vpY, vpW, vpH, rightX, rightY)) {
        return false;
    }

    float topX, topY, bottomX, bottomY;
    Vector3 top = center + cameraUp * (billboard.Size.y * 0.5f);
    Vector3 bottom = center - cameraUp * (billboard.Size.y * 0.5f);
    if (!worldToScreen(view, proj, top.x, top.y, top.z,
                       vpX, vpY, vpW, vpH, topX, topY)
        || !worldToScreen(view, proj, bottom.x, bottom.y, bottom.z,
                          vpX, vpY, vpW, vpH, bottomX, bottomY)) {
        return false;
    }

    out.width = std::abs(rightX - leftX);
    out.height = std::abs(bottomY - topY);
    if (out.width <= 0.0f || out.height <= 0.0f) return false;
    out.scaleX = out.width / billboard.Size.x;
    out.scaleY = out.height / billboard.Size.y;
    return true;
}

// ===================================================
//  WorldGuiObject の子 ScreenGuiObject を描画
// ===================================================
static void drawWorldGuiChildren(ImDrawList* dl, WorldGuiObject* wgo,
                                  float panelX, float panelY, float panelW, float panelH,
                                  std::function<void(GuiButton*)>& onActivated,
                                  float pixelScaleX = 1.0f, float pixelScaleY = 1.0f,
                                  bool worldMode = false) {
    // 子GUIが親パネルより大きい場合も、BillboardGuiのキャンバス外へ描画しない。
    dl->PushClipRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), true);
    for (auto& [name, child] : wgo->getChildren()) {
        if (!child->IsA("ScreenGuiObject")) continue;
        auto* sgo = static_cast<ScreenGuiObject*>(child.get());
        // Norm は WorldGui パネル内の相対座標として扱う
        float origNormX = sgo->Position.x, origNormY = sgo->Position.y;
        float origSizeX = sgo->Size.x,     origSizeY = sgo->Size.y;
        Norm origNorm = sgo->NormType;

        // パネル座標にオーバーライドして描画
        float px = (origNorm == Norm::Scale)
            ? origNormX * panelW + panelX
            : origNormX * pixelScaleX + panelX;
        float py = (origNorm == Norm::Scale)
            ? origNormY * panelH + panelY
            : origNormY * pixelScaleY + panelY;
        float sw = (origNorm == Norm::Scale) ? origSizeX * panelW : origSizeX * pixelScaleX;
        float sh = (origNorm == Norm::Scale) ? origSizeY * panelH : origSizeY * pixelScaleY;

        drawGuiContent(dl, sgo, px, py, sw, sh, &onActivated,
                       worldMode ? pixelScaleY : 1.0f,
                       /*fitTextToBounds=*/true);
    }
    dl->PopClipRect();
}

// ===================================================
//  SurfaceGui クリックヒットテスト用のマウスレイ生成
//  ViewportPanel.cpp の makeRay と同じ数式（45°FOV固定、forward/right/up基底）
// ===================================================
static Vector3 makeGuiRay(const GameGuiRenderContext& context, float mx, float my) {
    const float vpW = context.viewportWidth;
    const float vpH = context.viewportHeight;
    float ndcX  = (vpW > 0.f) ? (mx / vpW) * 2.0f - 1.0f : 0.0f;
    float ndcY  = (vpH > 0.f) ? 1.0f - (my / vpH) * 2.0f : 0.0f;
    float aspect = context.projectionAspect > 0.0f ? context.projectionAspect : 1.0f;
    float tanH  = std::tan(45.0f * (3.14159265f / 180.0f) * 0.5f);
    return (context.cameraForward
          + context.cameraRight * (ndcX * aspect * tanH)
          + context.cameraUp    * (ndcY * tanH)).normalize();
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
void Renderer::renderWorldGui(Workspace& ws, User* user, const GameGuiRenderContext& context) {
    const float vpX = context.viewportX;
    const float vpY = context.viewportY;
    const float vpW = context.viewportWidth;
    const float vpH = context.viewportHeight;
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
        const float localMouseX = mousePos.x - vpX;
        const float localMouseY = mousePos.y - vpY;
        const bool mouseInsideViewport = localMouseX >= 0.0f && localMouseY >= 0.0f
            && localMouseX <= vpW && localMouseY <= vpH;
        if (mouseInsideViewport) {
            Vector3 rayOri = context.cameraPosition;
            Vector3 rayDir = makeGuiRay(context, localMouseX, localMouseY);

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

            // BillboardGui / ProximityPromptのOffsetは親BaseCubeのローカル座標。
            // SurfaceGuiには適用せず、従来どおりフェイス中心へ描画する。
            BillboardGui* billboard = nullptr;
            if (wgo->IsA("BillboardGui")) {
                billboard = static_cast<BillboardGui*>(wgo);
                guiCenterOffset += billboard->Offset;
            }

            Vector3 guiCenter = cubeWorldCFrame.pointToWorld(guiCenterOffset);

            bool isProximityPrompt = (wgo->getClassName() == "ProximityPrompt");
            ProximityPrompt* pp = isProximityPrompt ? static_cast<ProximityPrompt*>(wgo) : nullptr;

            if (pp) {
                if (!pp->Enabled) continue;
                if (!SystemState::get().isPlaying) continue;

                // 距離チェック
                User* localUser = User::getInstance();
                if (!localUser || !localUser->humanoid) continue;
                auto root = localUser->humanoid->getRootPart();
                if (!root) continue;

                Vector3 playerPos = root->getWorldPosition();
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

            BillboardLayout billboardLayout;
            float sx, sy;
            float pw = wgo->Size.x, ph = wgo->Size.y;
            float childScaleX = 1.0f, childScaleY = 1.0f;
            bool worldSizeMode = billboard && billboard->SizeMode == BillboardSizeMode::World;
            if (worldSizeMode) {
                if (!computeBillboardLayout(context.view, context.projection, *billboard, guiCenter,
                                             vpX, vpY, vpW, vpH, billboardLayout)) {
                    continue;
                }
                sx = billboardLayout.centerX;
                sy = billboardLayout.centerY;
                pw = billboardLayout.width;
                ph = billboardLayout.height;
                childScaleX = billboardLayout.scaleX;
                childScaleY = billboardLayout.scaleY;
            } else {
                if (!worldToScreen(context.view, context.projection, guiCenter.x, guiCenter.y, guiCenter.z,
                                   vpX, vpY, vpW, vpH, sx, sy)) continue;
            }

            // パネル描画: スクリーン座標 (sx,sy) を中心にサイズを配置
            float panelX = sx - pw * 0.5f;
            float panelY = sy - ph * 0.5f;

            if (pp) {
                float promptScale = worldSizeMode ? (std::min)(childScaleX, childScaleY) : 1.0f;
                const Color4& bg = pp->BackgroundColor;
                ImU32 bgCol = IM_COL32((int)(bg.r*255),(int)(bg.g*255),(int)(bg.b*255),(int)(bg.a*255));
                dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), bgCol, 8.0f * promptScale);
                dl->AddRect(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), IM_COL32(255,255,255,80),
                            8.0f * promptScale, 0, 1.5f * promptScale);

                float textY = panelY + 8.0f * promptScale;
                float promptFontSize = ImGui::GetFontSize() * promptScale;
                if (!pp->ObjectText.empty()) {
                    ImVec2 baseTextSize = ImGui::CalcTextSize(pp->ObjectText.c_str());
                    ImVec2 textSize(baseTextSize.x * promptScale, baseTextSize.y * promptScale);
                    dl->AddText(ImGui::GetFont(), promptFontSize,
                                ImVec2(panelX + (pw - textSize.x)*0.5f, textY),
                                IM_COL32(200,200,200,255), pp->ObjectText.c_str());
                    textY += textSize.y + 4.0f * promptScale;
                }

                std::string actionStr = "[" + pp->KeyboardKeyCode + "] " + pp->ActionText;
                ImVec2 baseActionSize = ImGui::CalcTextSize(actionStr.c_str());
                ImVec2 actionSize(baseActionSize.x * promptScale, baseActionSize.y * promptScale);
                dl->AddText(ImGui::GetFont(), promptFontSize,
                            ImVec2(panelX + (pw - actionSize.x)*0.5f, textY),
                            IM_COL32(255,255,255,255), actionStr.c_str());

                if (pp->HoldDuration > 0.0f) {
                    float progress = pp->m_elapsedTime / pp->HoldDuration;
                    if (progress > 1.0f) progress = 1.0f;

                    float barMargin = 12.0f * promptScale;
                    float barY = panelY + ph - 12.0f * promptScale;
                    float barW = pw - barMargin * 2.0f;
                    float barH = 5.0f * promptScale;
                    dl->AddRectFilled(ImVec2(panelX + barMargin, barY), ImVec2(panelX + barMargin + barW, barY + barH), IM_COL32(50,50,50,255), 2.0f * promptScale);
                    if (progress > 0.0f) {
                        dl->AddRectFilled(ImVec2(panelX + barMargin, barY), ImVec2(panelX + barMargin + barW * progress, barY + barH), IM_COL32(100,255,100,255), 2.0f * promptScale);
                    }
                }

                drawWorldGuiChildren(dl, wgo, panelX, panelY, pw, ph, m_onButtonActivated,
                                     childScaleX, childScaleY, worldSizeMode);
                continue;
            }

            const Color4& bg = wgo->BackgroundColor;
            ImU32 bgCol = IM_COL32((int)(bg.r*255),(int)(bg.g*255),(int)(bg.b*255),(int)(bg.a*255));
            dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX+pw, panelY+ph), bgCol);

            drawWorldGuiChildren(dl, wgo, panelX, panelY, pw, ph, m_onButtonActivated,
                                 childScaleX, childScaleY, worldSizeMode);
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
GameGuiRenderContext Renderer::makeGameGuiRenderContext(
    float vpX, float vpY, float vpW, float vpH,
    const Vector3& cameraPosition,
    const Vector3& cameraForward,
    const Vector3& cameraRight,
    const Vector3& cameraUp,
    float projectionAspect,
    bool recordUserViewport) {
    GameGuiRenderContext context;
    context.viewportX = vpX;
    context.viewportY = vpY;
    context.viewportWidth = vpW;
    context.viewportHeight = vpH;
    context.projectionAspect = projectionAspect > 0.0f
        ? projectionAspect
        : ((vpW > 0.0f && vpH > 0.0f) ? vpW / vpH : 1.0f);
    context.cameraPosition = cameraPosition;
    context.cameraForward = cameraForward;
    context.cameraRight = cameraRight;
    context.cameraUp = cameraUp;
    context.view = Matrix4::LookAt(
        cameraPosition, cameraPosition + cameraForward, cameraUp);
    context.projection = Matrix4::Perspective(
        45.0f, context.projectionAspect, 0.1f, 10000.0f);
    context.recordUserViewport = recordUserViewport;
    return context;
}

void Renderer::renderGameGui(
    Workspace& ws, User* user, const GameGuiRenderContext& context) {
    if (user && context.recordUserViewport) {
        // GLFWのカーソル座標はメインウィンドウclient座標。ImGui multi-viewport時の
        // contextはデスクトップ座標なので、main viewportの原点を引いて明示変換する。
        const ImVec2 mainViewportPos = ImGui::GetMainViewport()->Pos;
        const bool viewportsEnabled =
            (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
        const double cursorCenterX = static_cast<double>(context.viewportX + context.viewportWidth * 0.5f
            - (viewportsEnabled ? mainViewportPos.x : 0.0f));
        const double cursorCenterY = static_cast<double>(context.viewportY + context.viewportHeight * 0.5f
            - (viewportsEnabled ? mainViewportPos.y : 0.0f));
        const bool cursorCenterValid = context.viewportWidth > 0.0f && context.viewportHeight > 0.0f;
        user->setGameViewport(
            context.viewportX, context.viewportY,
            context.viewportWidth, context.viewportHeight,
            context.projectionAspect,
            context.cameraPosition, context.cameraForward,
            context.cameraRight, context.cameraUp,
            cursorCenterX, cursorCenterY, cursorCenterValid);
    }
    renderScreenGui(
        ws, context.viewportX, context.viewportY,
        context.viewportWidth, context.viewportHeight);
    renderWorldGui(ws, user, context);
    if (user) {
        renderToolHotbar(
            *user, context.viewportX, context.viewportY,
            context.viewportWidth, context.viewportHeight);
    }
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
