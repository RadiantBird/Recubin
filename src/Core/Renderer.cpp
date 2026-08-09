#include <Core/Renderer.hpp>
#include <Core/Physics.hpp>
#include <Core/FileLoader.hpp>
#include <Util/Logger.hpp>
#include <Util/FrameProfiler.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/MeshEdges.hpp>
#include <Util/GLUniformCache.hpp>
#include <Editor/IEditorManager.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/LightSource.hpp>
#include <Instances/SpotLight.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Highlight.hpp>
#include <Instances/Model.hpp>
#include <Instances/Weather.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/Force.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/NoCollision.hpp>
#include <include/Math/PerlinNoise.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>
#include <include/Core/SceneRuntime.hpp>
#include <include/Instances/PostEffect.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <unordered_set>


#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "include/stb_image.h"
#include "include/Editor/IconsDef.hpp"

// ===================================================
//  ユーティリティ
// ===================================================

void Renderer::createWhiteTexture() {
    glGenTextures(1, &whiteTexture);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    unsigned char white[] = { 255, 255, 255, 0 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

Renderer* Renderer::instance = nullptr;

static Lighting* findLightingInTree(Instance* inst) {
    if (!inst) return nullptr;
    if (inst->IsA("Lighting")) return static_cast<Lighting*>(inst);
    for (auto& [name, child] : inst->getChildren()) {
        Lighting* found = findLightingInTree(child.get());
        if (found) return found;
    }
    return nullptr;
}

// Point/Spot ライトを再帰収集（複数光源シェーディング用）
static void collectLights(Instance* inst, std::vector<LightSource*>& out) {
    if (!inst) return;
    if (inst->IsA("LightSource")) out.push_back(static_cast<LightSource*>(inst));
    for (auto& [name, child] : inst->getChildren())
        collectLights(child.get(), out);
}

// ParticleEmitter を再帰収集（描画専用。シミュレーションはここでは行わない）
static void collectParticleEmitters(Instance* inst, std::vector<ParticleEmitter*>& out) {
    if (!inst) return;
    if (inst->IsA("ParticleEmitter")) out.push_back(static_cast<ParticleEmitter*>(inst));
    for (auto& [name, child] : inst->getChildren())
        collectParticleEmitters(child.get(), out);
}

// BaseCube派生の描画用VAO/インデックス数を、BaseCube::getHighlightVAO/getHighlightIndexCountから取得してバインドする。
// 戻り値: 描画可能なジオメトリがあれば true（VAOバインド済み・outIndexCountセット済み）
static bool bindHighlightGeometry(BaseCube* target, GLsizei& outIndexCount) {
    if (!target) return false;
    unsigned int vao = target->getHighlightVAO();
    unsigned int idx = target->getHighlightIndexCount();
    if (vao == 0 || idx == 0) return false;
    glBindVertexArray(vao);
    outIndexCount = (GLsizei)idx;
    return true;
}

// 独立した(連結ポリラインでない)線分群を、画面上で常に一定ピクセル幅に見えるリボンに変換する。
// segmentEndpoints: ワールド空間、セグメントごとに(x0,y0,z0,x1,y1,z1)のフラット配列。
// fovYDegrees/viewportHeightPxはMatrix4::Perspective()と同じ規約(垂直視野角・ピクセル高さ)。
static void buildSegmentRibbons(const std::vector<float>& segmentEndpoints,
                                 const Vector3& cameraPosition, float fovYDegrees,
                                 int viewportHeightPx, float pixelWidth,
                                 std::vector<float>& outVerts) {
    outVerts.clear();
    if (viewportHeightPx <= 0 || pixelWidth <= 0.001f) return;
    const float tanHalfFov = std::tan(fovYDegrees * 3.14159265f / 360.0f);
    for (size_t i = 0; i + 5 < segmentEndpoints.size(); i += 6) {
        Vector3 p0(segmentEndpoints[i],   segmentEndpoints[i+1], segmentEndpoints[i+2]);
        Vector3 p1(segmentEndpoints[i+3], segmentEndpoints[i+4], segmentEndpoints[i+5]);
        Vector3 segVec = p1 - p0;
        float len = segVec.length();
        if (len < 1e-6f) continue;
        Vector3 segDir = segVec / len;
        Vector3 mid = (p0 + p1) * 0.5f;
        Vector3 toCam = cameraPosition - mid;
        Vector3 viewDir = (toCam.length() > 1e-6f) ? toCam.normalize() : Vector3(0,1,0);
        Vector3 right = Vector3::Cross(segDir, viewDir);
        if (right.length() < 1e-5f) right = Vector3::Cross(segDir, Vector3(0,1,0));
        if (right.length() < 1e-5f) right = Vector3::Cross(segDir, Vector3(1,0,0));
        if (right.length() < 1e-5f) right = Vector3::Cross(segDir, Vector3(0,0,1));
        right = right.normalize();
        float d0 = (cameraPosition - p0).length();
        float d1 = (cameraPosition - p1).length();
        float half0 = pixelWidth * d0 * tanHalfFov / (float)viewportHeightPx;
        float half1 = pixelWidth * d1 * tanHalfFov / (float)viewportHeightPx;
        Vector3 a0 = p0 + right * half0, a1 = p0 - right * half0;
        Vector3 b0 = p1 + right * half1, b1 = p1 - right * half1;
        auto push = [&](const Vector3& v){ outVerts.push_back(v.x); outVerts.push_back(v.y); outVerts.push_back(v.z); };
        push(a0); push(a1); push(b1);
        push(a0); push(b1); push(b0);
    }
}

// 連結ポリライン(フラットな(x,y,z)*N配列)を、カメラ向きのリボン三角形(GL_TRIANGLES用フラット頂点配列)に
// 変換する。ワールド空間の全幅(width)を使う。各頂点で前後セグメント方向を平均した接線を使うため、
// ジョイント部に隙間ができない。m_lineShaderのaPosレイアウト(position-only vec3)にそのまま渡せる。
static void buildRibbonStrip(const std::vector<float>& points, float width,
                              const Vector3& cameraPos, std::vector<float>& outVerts) {
    size_t n = points.size() / 3;
    if (n < 2 || width <= 0.0f) return;
    float halfW = width * 0.5f;
    std::vector<Vector3> pts(n), left(n), right(n);
    for (size_t i = 0; i < n; ++i) pts[i] = Vector3(points[i*3], points[i*3+1], points[i*3+2]);
    for (size_t i = 0; i < n; ++i) {
        Vector3 dirPrev = (i > 0)     ? (pts[i] - pts[i-1]).normalize() : (pts[i+1] - pts[i]).normalize();
        Vector3 dirNext = (i+1 < n)   ? (pts[i+1] - pts[i]).normalize() : dirPrev;
        Vector3 tangent = (dirPrev + dirNext).normalize();
        Vector3 viewDir = (cameraPos - pts[i]).normalize();
        Vector3 side = Vector3::Cross(tangent, viewDir);
        float len = side.length();
        side = (len > 1e-5f) ? (side / len) * halfW : Vector3(halfW, 0, 0); // 縮退時のフォールバック
        left[i] = pts[i] + side; right[i] = pts[i] - side;
    }
    outVerts.reserve(outVerts.size() + (n - 1) * 18);
    auto push = [&](const Vector3& v){ outVerts.push_back(v.x); outVerts.push_back(v.y); outVerts.push_back(v.z); };
    for (size_t i = 0; i + 1 < n; ++i) {
        push(left[i]);  push(right[i]);   push(left[i+1]);
        push(right[i]); push(right[i+1]); push(left[i+1]);
    }
}

// inst以下の全子孫からBaseCubeをすべて再帰収集する（Model境界・BaseCube境界のどちらでも止まらない）
static void collectBaseCubesRecursive(Instance* inst, std::vector<BaseCube*>& out) {
    if (!inst) return;
    if (inst->IsA("BaseCube")) out.push_back(static_cast<BaseCube*>(inst));
    for (auto const& [name, child] : inst->getChildren())
        collectBaseCubesRecursive(child.get(), out);
}

// ハイライト対象の親（BaseCube単体 or Model）から、実際にハイライトすべきBaseCube群を収集する。
// - parent が BaseCube そのもの: そのBaseCube 1個のみ（子孫へは再帰しない）
// - parent が Model: 全子孫を再帰的に辿り、見つかったBaseCubeをすべて対象にする（入れ子Model含む）
static void collectHighlightTargets(Instance* parent, std::vector<BaseCube*>& out) {
    if (!parent) return;
    if (parent->IsA("Model")) {
        for (auto const& [name, child] : parent->getChildren())
            collectBaseCubesRecursive(child.get(), out);
    } else if (parent->IsA("BaseCube")) {
        out.push_back(static_cast<BaseCube*>(parent));
    }
}

// Highlight インスタンスを再帰収集（collectParticleEmittersと同じ形）
static void collectHighlightInstances(Instance* inst, std::vector<Highlight*>& out) {
    if (!inst) return;
    if (inst->IsA("Highlight")) out.push_back(static_cast<Highlight*>(inst));
    for (auto const& [name, child] : inst->getChildren())
        collectHighlightInstances(child.get(), out);
}

// ---- メインカメラパス用フラスタムカリング ----
// view*projection合成行列からGribb-Hartmann法で6平面(左右下上近遠)を抽出する。
// Matrix4はOpenGL準拠の列優先(column-major)なのでm[col*4+row]でアクセスする。
struct FrustumPlanes { float p[6][4]; };

static FrustumPlanes extractFrustumPlanes(const Matrix4& vp) {
    FrustumPlanes f;
    auto row = [&](int r) { return std::array<float,4>{ vp.m[0*4+r], vp.m[1*4+r], vp.m[2*4+r], vp.m[3*4+r] }; };
    auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto setPlane = [&](int idx, float a, float b, float c, float d) {
        float len = std::sqrt(a*a + b*b + c*c);
        if (len < 1e-6f) len = 1.0f;
        f.p[idx][0] = a / len; f.p[idx][1] = b / len; f.p[idx][2] = c / len; f.p[idx][3] = d / len;
    };
    setPlane(0, r3[0]+r0[0], r3[1]+r0[1], r3[2]+r0[2], r3[3]+r0[3]); // left
    setPlane(1, r3[0]-r0[0], r3[1]-r0[1], r3[2]-r0[2], r3[3]-r0[3]); // right
    setPlane(2, r3[0]+r1[0], r3[1]+r1[1], r3[2]+r1[2], r3[3]+r1[3]); // bottom
    setPlane(3, r3[0]-r1[0], r3[1]-r1[1], r3[2]-r1[2], r3[3]-r1[3]); // top
    setPlane(4, r3[0]+r2[0], r3[1]+r2[1], r3[2]+r2[2], r3[3]+r2[3]); // near
    setPlane(5, r3[0]-r2[0], r3[1]-r2[1], r3[2]-r2[2], r3[3]-r2[3]); // far
    return f;
}

// バウンディングスフィア(中心+半径)が視錐台と交差するか。回転しうるBaseCube系は
// 軸並行AABBだと不正確なため、対角線の半分を半径とする保守的な球で判定する。
static bool sphereInFrustum(const FrustumPlanes& f, const Vector3& center, float radius) {
    for (int i = 0; i < 6; i++) {
        float dist = f.p[i][0]*center.x + f.p[i][1]*center.y + f.p[i][2]*center.z + f.p[i][3];
        if (dist < -radius) return false;
    }
    return true;
}

// インスタンス描画できる「素のプリミティブ」の形状インデックスを返す。対象外は -1。
// 0=Cube, 1=Cylinder, 2=Sphere, 3=TriangularPrism（Renderer::m_instBatchesの並びと一致）
// クラス名完全一致のみ（Seat/Truss等の派生は独自描画の可能性があるため除外）。
// Cube/Cylinder等は面デカール描画を持つため、面子要素があれば個別描画にフォールバックする。
static int instanceableShapeIndex(BaseCube* bc) {
    std::string cn = bc->getClassName();
    int shapeIdx = -1;
    if      (cn == "Cube")            shapeIdx = 0;
    else if (cn == "Cylinder")        shapeIdx = 1;
    else if (cn == "Sphere")          shapeIdx = 2;
    else if (cn == "TriangularPrism") shapeIdx = 3;
    if (shapeIdx < 0) return -1;
    if (bc->Color.a < 0.999f) return -1;  // 半透明はブレンド順の問題があるため除外
    if (bc->Unlit || bc->UseTriplanar) return -1;
    if (bc->TextureScale != 1.0f) return -1;
    for (auto const& [name, child] : bc->getChildren()) {
        if (child->IsA("Decal") || child->IsA("Texture") ||
            child->getClassName() == "SurfaceGui" || child->getClassName() == "Canvas") {
            return -1;
        }
    }
    return shapeIdx;
}

// 形状の共有VAOにインスタンス属性(5-9, divisor=1)を後付けする。
// 非インスタンス描画時は uInstanced=0 でシェーダーが属性5-9を読まないため影響しない。
void Renderer::attachInstanceAttribs(unsigned int vao) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    for (int instAttrIdx = 0; instAttrIdx < 4; ++instAttrIdx) {
        glVertexAttribPointer(5 + instAttrIdx, 4, GL_FLOAT, GL_FALSE, sizeof(CubeInstanceData),
                              (void*)(sizeof(float) * 4 * instAttrIdx));
        glEnableVertexAttribArray(5 + instAttrIdx);
        glVertexAttribDivisor(5 + instAttrIdx, 1);
    }
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, sizeof(CubeInstanceData),
                          (void*)offsetof(CubeInstanceData, color));
    glEnableVertexAttribArray(9);
    glVertexAttribDivisor(9, 1);
    glBindVertexArray(0);
}

// ===================================================
//  init
// ===================================================
void Renderer::init(GLFWwindow* window) {
    instance  = this;
    m_window  = window;

    // ImGui 初期化（エディター/ランタイム両方でゲーム GUI 描画に必要）
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // NavEnableKeyboard は意図的に無効。Alt でメニュー層がフォーカスされる前時代的な挙動を避ける
#ifndef EDITOR_DISABLED
    // ドッキング/マルチビューポートはエディター専用
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    // GLFW backendが提供するモニターDPIをImGuiへ反映する。
    // フォントとDockingのプラットフォームウィンドウを、モニター移動時も追従させる。
    io.ConfigDpiScaleFonts    = true;
    io.ConfigDpiScaleViewports = true;
#else
    // ランタイムはレイアウトを永続化しない。エディターと同じフォルダで起動すると
    // 既定の imgui.ini（CWD相対）を共有してエディターのレイアウトを壊すため
    io.IniFilename = nullptr;
#endif

    ImGui::StyleColorsDark();

    // 日本語フォントが存在すれば使う。無ければ ImGui 既定フォントにフォールバック
    // （パッケージ版には assets/fonts/ が同梱されない場合があるため）
    if (std::filesystem::exists("assets/fonts/DotGothic16-Regular.ttf")) {
        io.Fonts->AddFontFromFileTTF("assets/fonts/DotGothic16-Regular.ttf", 22.0f, nullptr,
                                      io.Fonts->GetGlyphRangesJapanese());
    }
    if (std::filesystem::exists("assets/fonts/fa-solid-900.ttf")) {
        ImFontConfig cfg;
        cfg.MergeMode  = true;
        cfg.PixelSnapH = true;
        static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        io.Fonts->AddFontFromFileTTF("assets/fonts/fa-solid-900.ttf", 18.0f, &cfg, iconRanges);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // OpenGL バッファ
    glGenBuffers(1, &EBO);
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    for (int i = 0; i < 6; i++) {
        int start = i * 4;
        indices.push_back(start + 0); indices.push_back(start + 1); indices.push_back(start + 2);
        indices.push_back(start + 0); indices.push_back(start + 2); indices.push_back(start + 3);
    }

    std::string vShaderStr = FileLoader::readText("shaders/vertex.glsl");
    std::string fShaderStr = FileLoader::readText("shaders/fragment.glsl");
    const char* vertexShaderSource   = vShaderStr.c_str();
    const char* fragmentShaderSource = fShaderStr.c_str();

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    int  success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    char infoLog2[512];
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog2);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog2 << std::endl;
    }

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    char infoLog3[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog3);
        std::cout << "ERROR::SHADER::PROGRAM::LINK_FAILED\n" << infoLog3 << std::endl;
    }

    glUseProgram(shaderProgram);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glVertexAttrib1f(4, 1.0f); // MeshCube以外がattrib4を使わない場合のデフォルトMatAlpha=1

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // 標準キューブ頂点を VBO へ
    Cube testCube({0,0,0}, {1,1,1}, 0);
    glBindVertexArray(VAO);
    std::vector<Vertex> standardVertices = createCubeVertices(1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 standardVertices.size() * sizeof(Vertex),
                 standardVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);

    Cube::s_HighlightEdgeVerts = MeshEdges::extractHardEdges(
        reinterpret_cast<const float*>(standardVertices.data()), standardVertices.size(),
        8, 0, indices.data(), indices.size(), 20.0f);

    GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, U));
    glEnableVertexAttribArray(2);

    // --- インスタンスVBO生成。空だと属性5-9有効なVAOでの非インスタンス描画が範囲外読みに
    //     なりうるため、ゼロ埋め1インスタンス分を確保しておく ---
    glGenBuffers(1, &m_instanceVBO);
    CubeInstanceData zeroInst = {};
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CubeInstanceData), &zeroInst, GL_STREAM_DRAW);
    attachInstanceAttribs(VAO); // Cube::s_VAO(=VAO)はここで確定しているため即付与
    m_instBatches[0].attribsAttached = true;
    glBindVertexArray(VAO); // 以降の初期化は既存VAO前提のため戻す

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");
    glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);
    lightDirLoc   = glGetUniformLocation(shaderProgram, "lightDir");
    brightnessLoc = glGetUniformLocation(shaderProgram, "brightness");
    glUniform3f(lightDirLoc, 1.0f, -1.0f, -1.0f);
    glUniform1f(brightnessLoc, 1.0f);

    // --- 毎フレーム参照するuniform locationを一括キャッシュ（renderViewport/renderTerrainで再利用） ---
    viewLoc             = glGetUniformLocation(shaderProgram, "view");
    projectionLoc       = glGetUniformLocation(shaderProgram, "projection");
    viewPosLoc          = glGetUniformLocation(shaderProgram, "viewPos");
    hasShadowsLoc       = glGetUniformLocation(shaderProgram, "hasShadows");
    lightSpaceMatrixLoc = glGetUniformLocation(shaderProgram, "lightSpaceMatrix");
    modelLoc            = glGetUniformLocation(shaderProgram, "model");
    unlitLoc            = glGetUniformLocation(shaderProgram, "unlit");
    triplanarLoc        = glGetUniformLocation(shaderProgram, "useTriplanar");
    texScaleLoc         = glGetUniformLocation(shaderProgram, "u_textureScale");
    uTimeLoc            = glGetUniformLocation(shaderProgram, "uTime");
    uIsLiquidLoc        = glGetUniformLocation(shaderProgram, "uIsLiquid");
    useVertexColorLoc   = glGetUniformLocation(shaderProgram, "useVertexColor");
    ourColorLoc         = glGetUniformLocation(shaderProgram, "ourColor");
    uLightCountLoc      = glGetUniformLocation(shaderProgram, "uLightCount");
    m_uInstancedLoc     = glGetUniformLocation(shaderProgram, "uInstanced");
    for (int i = 0; i < MAX_LIGHTS; i++) {
        std::string b = "uLights[" + std::to_string(i) + "].";
        lightLocs[i].type       = glGetUniformLocation(shaderProgram, (b + "type").c_str());
        lightLocs[i].position   = glGetUniformLocation(shaderProgram, (b + "position").c_str());
        lightLocs[i].direction  = glGetUniformLocation(shaderProgram, (b + "direction").c_str());
        lightLocs[i].color      = glGetUniformLocation(shaderProgram, (b + "color").c_str());
        lightLocs[i].brightness = glGetUniformLocation(shaderProgram, (b + "brightness").c_str());
        lightLocs[i].range      = glGetUniformLocation(shaderProgram, (b + "range").c_str());
        lightLocs[i].cosCutoff  = glGetUniformLocation(shaderProgram, (b + "cosCutoff").c_str());
    }

    // --- Shadow map テクスチャユニットのバインド設定 ---
    glUniform1i(glGetUniformLocation(shaderProgram, "shadowMap"), 1);
    glUniform1f(hasShadowsLoc, 0.0f);

    // --- Depth シェーダーのコンパイル（Shadow Pass 用）---
    {
        std::string dvStr = FileLoader::readText("shaders/depth_vertex.glsl");
        std::string dfStr = FileLoader::readText("shaders/depth_fragment.glsl");
        const char* dvSrc = dvStr.c_str();
        const char* dfSrc = dfStr.c_str();

        unsigned int dv = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(dv, 1, &dvSrc, NULL);
        glCompileShader(dv);
        int ok; char log[512];
        glGetShaderiv(dv, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(dv, 512, NULL, log); std::cout << "DEPTH_VERT: " << log << std::endl; }

        unsigned int df = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(df, 1, &dfSrc, NULL);
        glCompileShader(df);
        glGetShaderiv(df, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(df, 512, NULL, log); std::cout << "DEPTH_FRAG: " << log << std::endl; }

        depthShader = glCreateProgram();
        glAttachShader(depthShader, dv);
        glAttachShader(depthShader, df);
        glLinkProgram(depthShader);
        glDeleteShader(dv);
        glDeleteShader(df);
        m_uInstancedDepthLoc = glGetUniformLocation(depthShader, "uInstanced");
    }

    // --- Shadow Map FBO + 深度テクスチャ生成 ---
    {
        glGenTextures(1, &shadowMapTex);
        glBindTexture(GL_TEXTURE_2D, shadowMapTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glGenFramebuffers(1, &shadowFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cout << "[Renderer] Shadow framebuffer is incomplete." << std::endl;
            glDeleteFramebuffers(1, &shadowFBO);
            glDeleteTextures(1, &shadowMapTex);
            shadowFBO = 0;
            shadowMapTex = 0;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    createWhiteTexture();
    Cube::defaultTextureID            = whiteTexture;
    Cube::s_VAO                       = VAO;
    Cube::s_EBO                       = EBO;
    Cylinder::defaultTextureID        = whiteTexture;
    TriangularPrism::defaultTextureID = whiteTexture;
    Sphere::defaultTextureID          = whiteTexture;
    stbi_set_flip_vertically_on_load(true);

    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);

    initLineRenderer();
    initPostEffectRenderer();
    initParticleRenderer();
    initCloudRenderer();
}

// ===================================================
//  デストラクタ
// ===================================================
Renderer::~Renderer() {
    if (instance == this) instance = nullptr;

    editor.reset(); // EditorManager を先に破棄（FBO が ImGui より先に解放される）

    // ImGui は init で両ビルド共に生成するため、破棄も両ビルドで行う
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &VAO);
    glDeleteProgram(shaderProgram);

    if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);

    if (shadowFBO)    glDeleteFramebuffers(1, &shadowFBO);
    if (shadowMapTex) glDeleteTextures(1, &shadowMapTex);
    if (depthShader)  glDeleteProgram(depthShader);

    if (m_lineVBO)    glDeleteBuffers(1, &m_lineVBO);
    if (m_lineVAO)    glDeleteVertexArrays(1, &m_lineVAO);
    if (m_lineShader) glDeleteProgram(m_lineShader);

    if (m_particleVBO)    glDeleteBuffers(1, &m_particleVBO);
    if (m_particleVAO)    glDeleteVertexArrays(1, &m_particleVAO);
    if (m_particleShader) glDeleteProgram(m_particleShader);

    if (m_cloudVBO)      glDeleteBuffers(1, &m_cloudVBO);
    if (m_cloudVAO)      glDeleteVertexArrays(1, &m_cloudVAO);
    if (m_cloudShader)   glDeleteProgram(m_cloudShader);
    if (m_cloudNoiseTex) glDeleteTextures(1, &m_cloudNoiseTex);

    if (m_postVBO)    glDeleteBuffers(1, &m_postVBO);
    if (m_postVAO)    glDeleteVertexArrays(1, &m_postVAO);
    if (m_postShader) glDeleteProgram(m_postShader);
    if (m_postFboA)   glDeleteFramebuffers(1, &m_postFboA);
    if (m_postTexA)   glDeleteTextures(1, &m_postTexA);
    if (m_postFboB)   glDeleteFramebuffers(1, &m_postFboB);
    if (m_postTexB)   glDeleteTextures(1, &m_postTexB);
}

// ===================================================
//  制約ビジュアライザ
// ===================================================
void Renderer::initLineRenderer() {
    const char* vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";
    const char* fs = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 lineColor;
void main() {
    FragColor = lineColor;
}
)";
    auto compile = [](const char* src, GLenum type) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        return sh;
    };
    GLuint v = compile(vs, GL_VERTEX_SHADER);
    GLuint f = compile(fs, GL_FRAGMENT_SHADER);
    m_lineShader = glCreateProgram();
    glAttachShader(m_lineShader, v);
    glAttachShader(m_lineShader, f);
    glLinkProgram(m_lineShader);
    glDeleteShader(v);
    glDeleteShader(f);

    glGenVertexArrays(1, &m_lineVAO);
    glGenBuffers(1, &m_lineVBO);
    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ===================================================
//  パーティクル（ビルボード・テクスチャなし単色頂点）
// ===================================================
void Renderer::initParticleRenderer() {
    const char* vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 view;
uniform mat4 projection;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";
    const char* fs = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";
    auto compile = [](const char* src, GLenum type) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        return sh;
    };
    GLuint v = compile(vs, GL_VERTEX_SHADER);
    GLuint f = compile(fs, GL_FRAGMENT_SHADER);
    m_particleShader = glCreateProgram();
    glAttachShader(m_particleShader, v);
    glAttachShader(m_particleShader, f);
    glLinkProgram(m_particleShader);
    glDeleteShader(v);
    glDeleteShader(f);

    glGenVertexArrays(1, &m_particleVAO);
    glGenBuffers(1, &m_particleVBO);
    glBindVertexArray(m_particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    GLsizei stride = 7 * sizeof(float); // aPos(3) + aColor(4)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// ===================================================
//  雲（Weather）
// ===================================================
void Renderer::initCloudRenderer() {
    const char* vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 view;
uniform mat4 projection;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";
    const char* fs = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D cloudTex;
uniform vec2  windOffset;
uniform float cloudCover;
uniform float cloudDensity;
void main() {
    float n = texture(cloudTex, vUV - windOffset).r;
    float alpha = smoothstep(1.0 - cloudCover, 1.0, n) * cloudDensity;
    FragColor = vec4(0.92, 0.93, 0.95, alpha);
}
)";
    auto compile = [](const char* src, GLenum type) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        return sh;
    };
    GLuint v = compile(vs, GL_VERTEX_SHADER);
    GLuint f = compile(fs, GL_FRAGMENT_SHADER);
    m_cloudShader = glCreateProgram();
    glAttachShader(m_cloudShader, v);
    glAttachShader(m_cloudShader, f);
    glLinkProgram(m_cloudShader);
    glDeleteShader(v);
    glDeleteShader(f);

    glGenVertexArrays(1, &m_cloudVAO);
    glGenBuffers(1, &m_cloudVBO);
    glBindVertexArray(m_cloudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVBO);
    GLsizei stride = 5 * sizeof(float); // aPos(3) + aUV(2)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // 固定シードのfbm2を512x512に焼き込む。シーン/Weatherに依存しない固定テクスチャで、
    // 実行時の再焼き込みは行わない（Terrainのノイズ生成と同じCPU計算+単純サンプルの流儀）。
    constexpr int   CLOUD_TEX_SIZE = 512;
    constexpr float CLOUD_SCALE    = 3.0f;
    PerlinNoise cloudNoise(1337u);
    std::vector<unsigned char> pixels(CLOUD_TEX_SIZE * CLOUD_TEX_SIZE);
    for (int y = 0; y < CLOUD_TEX_SIZE; y++) {
        for (int x = 0; x < CLOUD_TEX_SIZE; x++) {
            float u = (float)x / (float)CLOUD_TEX_SIZE;
            float w = (float)y / (float)CLOUD_TEX_SIZE;
            float fu = u * CLOUD_SCALE, fw = w * CLOUD_SCALE;

            // 四隅ブレンドでシームレスタイル化（GL_REPEATでのラップ点の不連続を消す）
            float n00 = cloudNoise.fbm2(fu,               fw,               6, 0.5f, 2.0f);
            float n10 = cloudNoise.fbm2(fu - CLOUD_SCALE,  fw,               6, 0.5f, 2.0f);
            float n01 = cloudNoise.fbm2(fu,                fw - CLOUD_SCALE, 6, 0.5f, 2.0f);
            float n11 = cloudNoise.fbm2(fu - CLOUD_SCALE,  fw - CLOUD_SCALE, 6, 0.5f, 2.0f);
            float n = n00 * (1.0f - u) * (1.0f - w)
                    + n10 * u          * (1.0f - w)
                    + n01 * (1.0f - u) * w
                    + n11 * u          * w;

            float n01c = n * 0.5f + 0.5f;
            pixels[y * CLOUD_TEX_SIZE + x] = static_cast<unsigned char>(std::clamp(n01c, 0.0f, 1.0f) * 255.0f);
        }
    }
    glGenTextures(1, &m_cloudNoiseTex);
    glBindTexture(GL_TEXTURE_2D, m_cloudNoiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, CLOUD_TEX_SIZE, CLOUD_TEX_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::renderClouds(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                             const Vector3& cameraPosition) {
    if (!m_cloudShader || !m_cloudNoiseTex) return;

    Weather* weather = nullptr;
    for (auto const& [name, child] : workspace.getChildren()) {
        if (child->IsA("Weather")) { weather = static_cast<Weather*>(child.get()); break; }
    }
    if (!weather || !weather->Enabled) return;

    float halfSize = 2000.0f;
    float y = cameraPosition.y + weather->CloudHeight;
    float cx = cameraPosition.x;
    float cz = cameraPosition.z;
    // UVは大きめのタイル数でスクロールを滑らかに見せる
    constexpr float UV_TILES = 8.0f;
    float verts[30] = {
        cx - halfSize, y, cz - halfSize,  0.0f,     0.0f,
        cx + halfSize, y, cz - halfSize,  UV_TILES, 0.0f,
        cx + halfSize, y, cz + halfSize,  UV_TILES, UV_TILES,

        cx + halfSize, y, cz + halfSize,  UV_TILES, UV_TILES,
        cx - halfSize, y, cz + halfSize,  0.0f,     UV_TILES,
        cx - halfSize, y, cz - halfSize,  0.0f,     0.0f,
    };

    Vector2 scroll = weather->getCloudScrollOffset();

    glUseProgram(m_cloudShader);
    glUniformMatrix4fv(glGetUniformLocation(m_cloudShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_cloudShader, "projection"), 1, GL_FALSE, projection.m);
    glUniform2f(glGetUniformLocation(m_cloudShader, "windOffset"), scroll.x, scroll.y);
    glUniform1f(glGetUniformLocation(m_cloudShader, "cloudCover"), weather->CloudCover);
    glUniform1f(glGetUniformLocation(m_cloudShader, "cloudDensity"), weather->CloudDensity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_cloudNoiseTex);
    glUniform1i(glGetUniformLocation(m_cloudShader, "cloudTex"), 0);

    glBindVertexArray(m_cloudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);

    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

void Renderer::renderLightning(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                                const Vector3& cameraPosition) {
    if (!m_lineShader) return;

    Weather* weather = nullptr;
    for (auto const& [name, child] : workspace.getChildren()) {
        if (child->IsA("Weather")) { weather = static_cast<Weather*>(child.get()); break; }
    }
    if (!weather || !weather->Enabled) return;

    float alpha = weather->getLightningBoltAlpha();
    const std::vector<Vector3>& bolt = weather->getLightningBolt();
    if (alpha <= 0.0f || bolt.size() < 2) return;

    std::vector<float> verts;
    verts.reserve(bolt.size() * 3);
    for (const Vector3& p : bolt) { verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z); }

    constexpr float kLightningWidth = 0.15f; // ワールド空間幅
    std::vector<float> ribbon;
    buildRibbonStrip(verts, kLightningWidth, cameraPosition, ribbon);
    if (ribbon.empty()) return;

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glUniform4f(glGetUniformLocation(m_lineShader, "lineColor"), 0.9f, 0.95f, 1.0f, alpha);

    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizei)(ribbon.size() * sizeof(float)), ribbon.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ribbon.size() / 3));
    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

void Renderer::renderConstraints(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                                  const Vector3& cameraPosition) {
    if (!m_lineShader) return;

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glBindVertexArray(m_lineVAO);

    int colorLoc = glGetUniformLocation(m_lineShader, "lineColor");

    auto uploadAndDraw = [&](const std::vector<float>& verts, float width) {
        std::vector<float> ribbon;
        buildRibbonStrip(verts, width, cameraPosition, ribbon);
        if (ribbon.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizei)(ribbon.size() * sizeof(float)), ribbon.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ribbon.size() / 3));
    };

    auto scan = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;

        if (inst->getClassName() == "Rope") {
            Rope* rope = static_cast<Rope*>(inst);
            auto c0 = rope->m_cube0.lock();
            auto c1 = rope->m_cube1.lock();
            if (c0 && c1) {
                auto a0 = rope->m_attachment0.lock();
                auto a1 = rope->m_attachment1.lock();
                Vector3 p0 = (a0 ? a0->getWorldCFrame() : c0->getWorldCFrame()).Position;
                Vector3 p1 = (a1 ? a1->getWorldCFrame() : c1->getWorldCFrame()).Position;
                float dist = (p1 - p0).length();
                float sag  = (rope->MaxDistance > dist) ? (rope->MaxDistance - dist) * 0.4f : 0.0f;
                // 二次ベジェによるカテナリー近似（制御点を重力方向にsagだけ下げる）
                Vector3 ctrl = (p0 + p1) * 0.5f + Vector3(0.0f, -sag, 0.0f);
                constexpr int SEG = 24;
                std::vector<float> verts;
                verts.reserve((SEG + 1) * 3);
                for (int i = 0; i <= SEG; i++) {
                    float t = (float)i / (float)SEG;
                    float mt = 1.0f - t;
                    Vector3 p = p0 * (mt * mt) + ctrl * (2.0f * mt * t) + p1 * (t * t);
                    verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
                }
                glUniform4f(colorLoc, rope->Color.r, rope->Color.g, rope->Color.b, rope->Color.a);
                uploadAndDraw(verts, rope->LineWidth);
            }
        } else if (inst->getClassName() == "Rod") {
            Rod* rod = static_cast<Rod*>(inst);
            auto c0 = rod->m_cube0.lock();
            auto c1 = rod->m_cube1.lock();
            if (c0 && c1) {
                auto a0 = rod->m_attachment0.lock();
                auto a1 = rod->m_attachment1.lock();
                Vector3 p0 = (a0 ? a0->getWorldCFrame() : c0->getWorldCFrame()).Position;
                Vector3 p1 = (a1 ? a1->getWorldCFrame() : c1->getWorldCFrame()).Position;
                std::vector<float> verts = {
                    p0.x, p0.y, p0.z,
                    p1.x, p1.y, p1.z
                };
                glUniform4f(colorLoc, rod->Color.r, rod->Color.g, rod->Color.b, rod->Color.a);
                uploadAndDraw(verts, rod->LineWidth);
            }
        }

        for (auto const& [name, child] : inst->getChildren()) {
            self(self, child.get());
        }
    };

    for (auto const& [name, child] : workspace.getChildren()) {
        scan(scan, child.get());
    }

    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

// ===================================================
//  物理制約デバッグビジュアライザー（Viewメニュー「Physics Debug」で切替）
//  Weld: 接続線＋中点クロス / Motor: ピボット・軸・回転方向の円弧矢印
//  Attachment: ワイヤ球＋向きの軸線 / Force: 力の矢印またはトルクの円弧矢印
// ===================================================
void Renderer::renderPhysicsDebug(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                                   const Vector3& cameraPosition) {
    if (!m_lineShader) return;

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glBindVertexArray(m_lineVAO);

    int colorLoc = glGetUniformLocation(m_lineShader, "lineColor");

    const Color4 WELD_COLOR   {0.25f, 1.0f,  0.4f,  1.0f};
    const Color4 MOTOR_COLOR  {1.0f,  0.85f, 0.2f,  1.0f};
    const Color4 ATTACH_COLOR {1.0f,  0.55f, 0.15f, 1.0f};
    const Color4 FORCE_COLOR  {1.0f,  0.3f,  0.25f, 1.0f};
    const Color4 BALLSOCKET_COLOR {0.45f, 0.6f,  1.0f,  1.0f};
    const Color4 NOCOLL_COLOR     {1.0f,  0.35f, 0.35f, 1.0f};

    constexpr float kPhysicsDebugWidth = 0.05f; // ワールド空間幅（旧glLineWidth(2.0f)相当）
    auto uploadAndDraw = [&](const std::vector<float>& verts) {
        std::vector<float> ribbon;
        buildRibbonStrip(verts, kPhysicsDebugWidth, cameraPosition, ribbon);
        if (ribbon.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizei)(ribbon.size() * sizeof(float)), ribbon.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ribbon.size() / 3));
    };
    auto setColor = [&](const Color4& c) { glUniform4f(colorLoc, c.r, c.g, c.b, c.a); };
    auto drawSegment = [&](const Vector3& a, const Vector3& b) {
        uploadAndDraw({a.x, a.y, a.z, b.x, b.y, b.z});
    };
    // axis に垂直な正規直交基底 (u, v) を作る
    auto basisFor = [](const Vector3& axis, Vector3& u, Vector3& v) {
        Vector3 n = axis.normalize();
        Vector3 helper = (std::fabs(n.y) < 0.9f) ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
        u = Vector3::Cross(n, helper).normalize();
        v = Vector3::Cross(n, u); // n,u が正規直交なので正規化済み
    };
    // center を通る axis 周りの円（全周）
    auto drawCircle = [&](const Vector3& center, const Vector3& axis, float radius) {
        Vector3 u, v; basisFor(axis, u, v);
        constexpr int SEG = 24;
        std::vector<float> verts;
        verts.reserve((SEG + 1) * 3);
        for (int i = 0; i <= SEG; i++) {
            float t = (float)i / SEG * 2.0f * 3.14159265f;
            Vector3 p = center + (u * std::cos(t) + v * std::sin(t)) * radius;
            verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        }
        uploadAndDraw(verts);
    };
    // 3/4 円弧＋終端の矢印ヘッド（回転方向の表示。sign<0 で逆回り）
    auto drawRotationArc = [&](const Vector3& center, const Vector3& axis, float radius, float sign) {
        Vector3 u, v; basisFor(axis, u, v);
        constexpr int SEG = 18;
        const float SWEEP = 1.5f * 3.14159265f; // 3/4周
        std::vector<float> verts;
        verts.reserve((SEG + 1) * 3);
        Vector3 end, tangent;
        for (int i = 0; i <= SEG; i++) {
            float t = (float)i / SEG * SWEEP * (sign < 0.0f ? -1.0f : 1.0f);
            Vector3 p = center + (u * std::cos(t) + v * std::sin(t)) * radius;
            verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
            if (i == SEG) {
                end     = p;
                tangent = (u * -std::sin(t) + v * std::cos(t)) * (sign < 0.0f ? -1.0f : 1.0f);
            }
        }
        uploadAndDraw(verts);
        // 矢印ヘッド: 終端から進行方向の逆へ、半径方向±に開く2本
        Vector3 radial = (end - center).normalize();
        Vector3 back   = end - tangent * (radius * 0.35f);
        drawSegment(end, back + radial * (radius * 0.18f));
        drawSegment(end, back - radial * (radius * 0.18f));
    };
    // 点マーカー: 3軸の小さなクロス
    auto drawCross = [&](const Vector3& p, float r) {
        drawSegment(p - Vector3(r, 0, 0), p + Vector3(r, 0, 0));
        drawSegment(p - Vector3(0, r, 0), p + Vector3(0, r, 0));
        drawSegment(p - Vector3(0, 0, r), p + Vector3(0, 0, r));
    };
    // 矢印（力の可視化）
    auto drawArrow = [&](const Vector3& from, const Vector3& dir, float length) {
        Vector3 n   = dir.normalize();
        Vector3 tip = from + n * length;
        drawSegment(from, tip);
        Vector3 u, v; basisFor(n, u, v);
        float h = length * 0.15f;
        drawSegment(tip, tip - n * h + u * (h * 0.6f));
        drawSegment(tip, tip - n * h - u * (h * 0.6f));
        drawSegment(tip, tip - n * h + v * (h * 0.6f));
        drawSegment(tip, tip - n * h - v * (h * 0.6f));
    };

    auto scan = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        const std::string cn = inst->getClassName();

        if (cn == "Weld") {
            Weld* weld = static_cast<Weld*>(inst);
            auto c0 = weld->m_cube0.lock();
            auto c1 = weld->m_cube1.lock();
            if (c0 && c1) {
                setColor(WELD_COLOR);
                Vector3 p0 = c0->getWorldCFrame().Position;
                Vector3 p1 = c1->getWorldCFrame().Position;
                drawSegment(p0, p1);
                drawCross((p0 + p1) * 0.5f, 0.2f);
            }
        } else if (cn == "Motor") {
            Motor* motor = static_cast<Motor*>(inst);
            auto c0 = motor->m_cube0.lock();
            auto c1 = motor->m_cube1.lock();
            if (c0 && c1) {
                // ピボット位置は createMotor と同じ規則（Attachment優先、無ければ中点）
                auto a0 = motor->m_attachment0.lock();
                auto a1 = motor->m_attachment1.lock();
                Vector3 pivot;
                if (a0 && a1) pivot = (a0->getWorldCFrame().Position + a1->getWorldCFrame().Position) * 0.5f;
                else if (a0)  pivot = a0->getWorldCFrame().Position;
                else if (a1)  pivot = a1->getWorldCFrame().Position;
                else          pivot = (c0->getWorldCFrame().Position + c1->getWorldCFrame().Position) * 0.5f;

                setColor(MOTOR_COLOR);
                Vector3 axis = c0->getWorldCFrame().Rotation.rotate(motor->Axis).normalize();
                drawSegment(pivot - axis * 1.5f, pivot + axis * 1.5f); // 回転軸
                drawCross(pivot, 0.12f);                               // ピボット
                if (motor->DriveVelocity != 0.0f)
                    drawRotationArc(pivot, axis, 0.75f, motor->DriveVelocity); // 回転方向
            }
        } else if (cn == "Attachment") {
            Attachment* att = static_cast<Attachment*>(inst);
            setColor(ATTACH_COLOR);
            CFrame wcf = att->getWorldCFrame();
            constexpr float R = 0.18f;
            // 「丸っぽい」マーカー: 直交3円のワイヤ球
            drawCircle(wcf.Position, wcf.Rotation.getRight(),   R);
            drawCircle(wcf.Position, wcf.Rotation.getUp(),      R);
            drawCircle(wcf.Position, wcf.Rotation.getForward(), R);
            // 向きが分かる軸線（ローカルX方向）
            drawSegment(wcf.Position, wcf.Position + wcf.Rotation.getRight() * (R * 2.5f));
        } else if (cn == "Force") {
            Force* force = static_cast<Force*>(inst);
            auto parent = inst->Parent.lock();
            if (force->Enabled && parent && parent->IsA("BaseCube") && force->Value.length() > 0.0001f) {
                setColor(FORCE_COLOR);
                Vector3 origin = static_cast<Spatial*>(parent.get())->getWorldCFrame().Position;
                if (force->Torque) {
                    // 角力: Value 軸線と、その周りの回転方向の円弧矢印
                    Vector3 axis = force->Value.normalize();
                    drawSegment(origin - axis * 1.2f, origin + axis * 1.2f);
                    drawRotationArc(origin, axis, 1.0f, 1.0f);
                } else {
                    // ベクトル力: 大きさに応じた長さの矢印（1〜4studにクランプ）
                    float len = std::min(std::max(force->Value.length() * 0.01f, 1.0f), 4.0f);
                    drawArrow(origin, force->Value, len);
                }
            }
        } else if (cn == "BallSocket") {
            BallSocket* bs = static_cast<BallSocket*>(inst);
            auto c0 = bs->m_cube0.lock();
            auto c1 = bs->m_cube1.lock();
            if (c0 && c1) {
                // ピボット位置は Motor と同じ規則（Attachment優先、無ければ中点）
                auto a0 = bs->m_attachment0.lock();
                auto a1 = bs->m_attachment1.lock();
                Vector3 pivot;
                if (a0 && a1) pivot = (a0->getWorldCFrame().Position + a1->getWorldCFrame().Position) * 0.5f;
                else if (a0)  pivot = a0->getWorldCFrame().Position;
                else if (a1)  pivot = a1->getWorldCFrame().Position;
                else          pivot = (c0->getWorldCFrame().Position + c1->getWorldCFrame().Position) * 0.5f;

                setColor(BALLSOCKET_COLOR);
                // ボール部: ピボットのワイヤ球（直交3円）
                constexpr float R = 0.3f;
                drawCircle(pivot, Vector3(1, 0, 0), R);
                drawCircle(pivot, Vector3(0, 1, 0), R);
                drawCircle(pivot, Vector3(0, 0, 1), R);
                // ソケット部: 両Cube中心からピボットへの接続線
                drawSegment(c0->getWorldCFrame().Position, pivot);
                drawSegment(c1->getWorldCFrame().Position, pivot);
            }
        } else if (cn == "NoCollision") {
            NoCollision* nc = static_cast<NoCollision*>(inst);
            auto c0 = nc->m_cube0.lock();
            auto c1 = nc->m_cube1.lock();
            if (c0 && c1) {
                setColor(NOCOLL_COLOR);
                Vector3 p0 = c0->getWorldCFrame().Position;
                Vector3 p1 = c1->getWorldCFrame().Position;
                drawSegment(p0, p1);
                // 中点にカメラ向きの「禁止」マーク（円＋斜線）
                Vector3 mid = (p0 + p1) * 0.5f;
                Vector3 toCam = cameraPosition - mid;
                Vector3 axis = (toCam.length() > 0.0001f) ? toCam.normalize() : Vector3(0, 1, 0);
                constexpr float R = 0.35f;
                drawCircle(mid, axis, R);
                Vector3 u, v; basisFor(axis, u, v);
                Vector3 diag = (u + v).normalize() * R;
                drawSegment(mid - diag, mid + diag);
            }
        }

        for (auto const& [name, child] : inst->getChildren())
            self(self, child.get());
    };
    for (auto const& [name, child] : workspace.getChildren())
        scan(scan, child.get());

    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

// ===================================================
//  パーティクル描画
// ===================================================
void Renderer::renderParticles(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                                const Vector3& cameraRight, const Vector3& cameraUp) {
    if (!m_particleShader) return;

    std::vector<ParticleEmitter*> emitters;
    collectParticleEmitters(static_cast<Instance*>(&workspace), emitters);
    if (emitters.empty()) return;

    Vector3 billR = cameraRight;
    Vector3 billU = cameraUp;

    std::vector<float> verts; // aPos(3) + aColor(4) 頂点ごと。三角形2枚=6頂点/粒子
    for (ParticleEmitter* emitter : emitters) {
        const Color4& startColor = emitter->StartColor;
        const Color4& endColor   = emitter->EndColor;
        float startSize = emitter->StartSize;
        float endSize   = emitter->EndSize;

        for (const auto& p : emitter->getParticles()) {
            float t = (p.lifetime > 0.0001f) ? (p.age / p.lifetime) : 1.0f;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;

            Color4 color = startColor + (endColor - startColor) * t;
            float  half  = (startSize + (endSize - startSize) * t) * 0.5f;

            float cs = std::cos(p.spinAngle);
            float sn = std::sin(p.spinAngle);
            Vector3 rotR = billR * cs + billU * sn;
            Vector3 rotU = billU * cs - billR * sn;

            Vector3 corners[4] = {
                p.position + (rotR * -half) + (rotU * -half),
                p.position + (rotR *  half) + (rotU * -half),
                p.position + (rotR *  half) + (rotU *  half),
                p.position + (rotR * -half) + (rotU *  half),
            };
            int order[6] = {0, 1, 2, 2, 3, 0};
            for (int idx : order) {
                const Vector3& c = corners[idx];
                verts.insert(verts.end(), {c.x, c.y, c.z, color.r, color.g, color.b, color.a});
            }
        }
    }
    if (verts.empty()) return;

    glUseProgram(m_particleShader);
    glUniformMatrix4fv(glGetUniformLocation(m_particleShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_particleShader, "projection"), 1, GL_FALSE, projection.m);

    glBindVertexArray(m_particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizei)(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);

    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 7));
    glDepthMask(GL_TRUE);

    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

// ===================================================
//  地形ブラシのヒット位置ガイド（ヒット面法線に直交するリング）
// ===================================================
void Renderer::renderBrushMarker(const Matrix4& view, const Matrix4& projection,
                                 const Vector3& center, float radius,
                                 const Vector3& cameraPosition, const Vector3& normal) {
    if (!m_lineShader || radius <= 0.0f) return;

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glUniform4f(glGetUniformLocation(m_lineShader, "lineColor"), 1.0f, 0.85f, 0.2f, 1.0f);

    // 法線を正規化（ゼロベクトル対策としてワールドUpにフォールバック）
    Vector3 n = normal.normalize();
    if (n.length() < 0.5f) n = Vector3(0.0f, 1.0f, 0.0f);

    // 法線に直交する接線ベクトルを求める。法線がワールドUpに近い場合は
    // 外積がゼロベクトルになるのを避けるためワールドForwardを基準にする。
    const Vector3 worldUp(0.0f, 1.0f, 0.0f);
    const Vector3 worldForward(0.0f, 0.0f, 1.0f);
    Vector3 refUp = (std::abs(Vector3::Dot(n, worldUp)) > 0.99f) ? worldForward : worldUp;
    Vector3 tangentU = Vector3::Cross(refUp, n).normalize();
    Vector3 tangentV = Vector3::Cross(n, tangentU);

    // 地表とのZファイト回避に法線方向へ少し浮かせる
    Vector3 normalOffset = n * 0.05f;

    constexpr int SEG = 48;
    std::vector<float> verts;
    verts.reserve((SEG + 1) * 3);
    for (int i = 0; i <= SEG; i++) {
        float a = (float)i / (float)SEG * 6.28318530718f;
        Vector3 p = center + normalOffset + tangentU * (std::cos(a) * radius) + tangentV * (std::sin(a) * radius);
        verts.push_back(p.x);
        verts.push_back(p.y);
        verts.push_back(p.z);
    }

    constexpr float kBrushMarkerWidth = 0.1f; // ワールド空間幅
    std::vector<float> ribbon;
    buildRibbonStrip(verts, kBrushMarkerWidth, cameraPosition, ribbon);
    if (ribbon.empty()) { glUseProgram(shaderProgram); return; }

    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizei)(ribbon.size() * sizeof(float)), ribbon.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ribbon.size() / 3));
    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

// ===================================================
//  ポストエフェクト（PostEffect インスタンスの ZIndex 順チェーン適用）
// ===================================================
void Renderer::initPostEffectRenderer() {
    std::string vStr = FileLoader::readText("shaders/postprocess_vertex.glsl");
    std::string fStr = FileLoader::readText("shaders/postprocess_fragment.glsl");
    const char* vSrc = vStr.c_str();
    const char* fSrc = fStr.c_str();

    unsigned int v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vSrc, NULL);
    glCompileShader(v);
    int ok; char log[512];
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(v, 512, NULL, log); std::cout << "POSTFX_VERT: " << log << std::endl; }

    unsigned int f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fSrc, NULL);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(f, 512, NULL, log); std::cout << "POSTFX_FRAG: " << log << std::endl; }

    m_postShader = glCreateProgram();
    glAttachShader(m_postShader, v);
    glAttachShader(m_postShader, f);
    glLinkProgram(m_postShader);
    glDeleteShader(v);
    glDeleteShader(f);

    // NDC -1..1 を覆うフルスクリーンクアッド（位置 vec2 + UV vec2）
    float quadVerts[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };

    glGenVertexArrays(1, &m_postVAO);
    glGenBuffers(1, &m_postVBO);
    glBindVertexArray(m_postVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_postVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::ensurePostEffectFBOs(int width, int height) {
    if (width == m_postFboWidth && height == m_postFboHeight && m_postFboA && m_postFboB) return;

    if (m_postFboA) glDeleteFramebuffers(1, &m_postFboA);
    if (m_postTexA) glDeleteTextures(1, &m_postTexA);
    if (m_postFboB) glDeleteFramebuffers(1, &m_postFboB);
    if (m_postTexB) glDeleteTextures(1, &m_postTexB);

    auto makeFbo = [&](GLuint& fbo, GLuint& tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    };
    makeFbo(m_postFboA, m_postTexA);
    makeFbo(m_postFboB, m_postTexB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_postFboWidth  = width;
    m_postFboHeight = height;
}

void Renderer::renderPostEffects(Workspace& workspace, GLuint targetFbo, int width, int height) {
    if (!m_postShader || width <= 0 || height <= 0) return;

    std::vector<PostEffect*> effects;
    auto collect = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        if (inst->IsA("PostEffect")) {
            auto* pe = static_cast<PostEffect*>(inst);
            if (pe->Enabled) effects.push_back(pe);
        }
        for (auto const& [name, child] : inst->getChildren()) {
            self(self, child.get());
        }
    };
    for (auto const& [name, child] : workspace.getChildren()) {
        collect(collect, child.get());
    }
    if (effects.empty()) return;

    std::sort(effects.begin(), effects.end(), [](PostEffect* a, PostEffect* b) {
        return a->ZIndex < b->ZIndex;
    });

    ensurePostEffectFBOs(width, height);

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // targetFbo の色情報を最初のスクラッチテクスチャへコピー
    glBindFramebuffer(GL_READ_FRAMEBUFFER, targetFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_postFboA);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glUseProgram(m_postShader);
    glBindVertexArray(m_postVAO);
    glUniform1i(glGetUniformLocation(m_postShader, "screenTexture"), 0);
    glUniform2f(glGetUniformLocation(m_postShader, "u_resolution"), (float)width, (float)height);
    glUniform1f(glGetUniformLocation(m_postShader, "u_time"), (float)glfwGetTime());
    int typeLoc      = glGetUniformLocation(m_postShader, "u_effectType");
    int intensityLoc = glGetUniformLocation(m_postShader, "u_intensity");
    int param1Loc    = glGetUniformLocation(m_postShader, "u_param1");
    int param2Loc    = glGetUniformLocation(m_postShader, "u_param2");
    glActiveTexture(GL_TEXTURE0);

    GLuint srcTex = m_postTexA;
    GLuint pingFbo = m_postFboB; // 次の書き込み先（ping-pong）
    GLuint pingTex = m_postTexB;

    for (size_t i = 0; i < effects.size(); i++) {
        PostEffect* effect = effects[i];
        bool isLast = (i == effects.size() - 1);
        GLuint destFbo = isLast ? targetFbo : pingFbo;

        glBindFramebuffer(GL_FRAMEBUFFER, destFbo);
        glViewport(0, 0, width, height);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glUniform1i(typeLoc, static_cast<int>(effect->Type));
        glUniform1f(intensityLoc, effect->Intensity);
        glUniform1f(param1Loc, effect->Param1);
        glUniform1f(param2Loc, effect->Param2);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (!isLast) {
            srcTex = pingTex;
            pingFbo = (pingFbo == m_postFboB) ? m_postFboA : m_postFboB;
            pingTex = (pingTex == m_postTexB) ? m_postTexA : m_postTexB;
        }
    }

    glBindVertexArray(0);
    glUseProgram(shaderProgram);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    if (blendWasEnabled) glEnable(GL_BLEND);
}

void Renderer::drawBaseCubeHighlight(BaseCube* target, const Color4& fillColor,
                                      const Color4& outlineColor, float outlineThickness,
                                      const Matrix4& view, const Matrix4& projection,
                                      const Vector3& cameraPosition, float fovYDegrees,
                                      int viewportHeightPx) {
    if (!target) return;

    GLsizei indexCount = 0;
    if (!bindHighlightGeometry(target, indexCount)) return;

    glDisable(GL_DEPTH_TEST);                       // 深度無効化：壁越しに見せる
    if (unlitLoc != -1) glUniform1f(unlitLoc, 1.0f); // フラット単色描画を保証（メインパスの残り値に依存しない）
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);

    // ---- 単色塗り ----
    if (fillColor.a > 0.001f) {
        Matrix4 fillMat = target->getWorldCFrame().toMatrix4() *
                          Matrix4::Scale(target->Size.x, target->Size.y, target->Size.z);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, fillMat.m);
        if (ourColorLoc != -1) glUniform4f(ourColorLoc, fillColor.r, fillColor.g, fillColor.b, fillColor.a);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    }

    // ---- 外側の縁取り（硬いエッジを画面空間幅リボン化） ----
    if (outlineColor.a > 0.001f && outlineThickness > 0.001f && m_lineShader) {
        const std::vector<float>& localEdges = target->getHighlightEdgeVerts();
        if (!localEdges.empty()) {
            CFrame wcf = target->getWorldCFrame();
            Vector3 size = target->Size;
            std::vector<float> worldSegs(localEdges.size());
            for (size_t i = 0; i + 2 < localEdges.size(); i += 3) {
                Vector3 local(localEdges[i] * size.x, localEdges[i+1] * size.y, localEdges[i+2] * size.z);
                Vector3 world = wcf.Position + wcf.Rotation.rotate(local);
                worldSegs[i] = world.x; worldSegs[i+1] = world.y; worldSegs[i+2] = world.z;
            }

            std::vector<float> ribbonVerts;
            buildSegmentRibbons(worldSegs, cameraPosition, fovYDegrees, viewportHeightPx, outlineThickness, ribbonVerts);

            if (!ribbonVerts.empty()) {
                glUseProgram(m_lineShader);
                glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
                glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
                glUniform4f(glGetUniformLocation(m_lineShader, "lineColor"), outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a);
                glBindVertexArray(m_lineVAO);
                glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
                glBufferData(GL_ARRAY_BUFFER, (GLsizei)(ribbonVerts.size() * sizeof(float)), ribbonVerts.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(ribbonVerts.size() / 3));
                glBindVertexArray(0);
                glUseProgram(shaderProgram); // 塗りパス等が前提とするメインシェーダーに戻す
            }
        }
    }

    if (unlitLoc != -1) glUniform1f(unlitLoc, 0.0f);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawTransientHighlight(BaseCube* target, const Color4& fillColor,
                                      const Color4& outlineColor, float outlineThickness,
                                      const Matrix4& view, const Matrix4& projection,
                                      const Vector3& cameraPosition, float fovYDegrees,
                                      int viewportHeightPx) {
    drawBaseCubeHighlight(target, fillColor, outlineColor, outlineThickness,
                          view, projection, cameraPosition, fovYDegrees, viewportHeightPx);
}

void Renderer::renderInstanceHighlights(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                                         const Vector3& cameraPosition, float fovYDegrees, int viewportHeightPx) {
    std::vector<Highlight*> highlights;
    for (auto const& [name, child] : workspace.getChildren())
        collectHighlightInstances(child.get(), highlights);

    for (Highlight* hl : highlights) {
        if (!hl->Enabled) continue;
        auto parentSp = hl->Parent.lock();
        if (!parentSp) continue;
        std::vector<BaseCube*> targets;
        collectHighlightTargets(parentSp.get(), targets);
        for (BaseCube* bc : targets)
            drawBaseCubeHighlight(bc, hl->FillColor, hl->OutlineColor, hl->OutlineThickness,
                                  view, projection, cameraPosition, fovYDegrees, viewportHeightPx);
    }
}

// ===================================================
//  統合されたビューポート描画
// ===================================================
void Renderer::renderViewport(const ViewportRenderDesc& desc) {
    if (!desc.workspace || desc.width <= 0 || desc.height <= 0) return;

    GLint prevFBO = 0;
    GLint prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, desc.fbo);
    glViewport(0, 0, desc.width, desc.height);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)desc.width / (float)desc.height;
    const float fovYDegrees = 45.0f;
    Matrix4 projection = Matrix4::Perspective(fovYDegrees, aspect, 0.1f, 10000.0f);
    Matrix4 view       = Matrix4::LookAt(desc.cameraPosition, desc.cameraPosition + desc.cameraForward, desc.cameraUp);

    // メインカメラパスのフラスタムカリング用（BaseCube系の描画ループでのみ使用。
    // Shadow Pass/Terrainは対象外）
    FrustumPlanes camFrustum = extractFrustumPlanes(projection * view);

    // ---- インスタンス描画対象4形状の {VAO, インデックス数}。s_VAOは遅延生成なので毎回参照 ----
    struct InstShapeInfo { unsigned int vao; int indexCount; };
    const InstShapeInfo instShapes[INST_SHAPE_COUNT] = {
        { Cube::s_VAO,            36 },
        { Cylinder::s_VAO,        Cylinder::s_IndexCount },
        { Sphere::s_VAO,          Sphere::s_IndexCount },
        { TriangularPrism::s_VAO, TriangularPrism::s_IndexCount },
    };
    for (int shapeIdx = 0; shapeIdx < INST_SHAPE_COUNT; ++shapeIdx) {
        if (!m_instBatches[shapeIdx].attribsAttached && instShapes[shapeIdx].vao != 0) {
            attachInstanceAttribs(instShapes[shapeIdx].vao);
            m_instBatches[shapeIdx].attribsAttached = true;
        }
    }

    // Workspace 内から Lighting を取得
    Lighting* lighting = findLightingInTree(static_cast<Instance*>(desc.workspace));

    // Sun・Moon の位置を毎フレーム Angle から再計算（フォーカス外でも Angle 変更を即反映するため）
    {
        Sun*  sunInst  = nullptr;
        Moon* moonInst = nullptr;
        for (auto const& [name, child] : desc.workspace->getChildren()) {
            if (child->IsA("Sun"))       sunInst  = static_cast<Sun*>(child.get());
            else if (child->IsA("Moon")) moonInst = static_cast<Moon*>(child.get());
        }
        if (sunInst) {
            float rad = sunInst->Angle * (3.14159265f / 180.0f);
            Vector3 sunDir(0.0f, std::sin(rad), std::cos(rad));
            sunInst->teleportTo(desc.cameraPosition + sunDir * 1000.0f);
            if (moonInst) {
                moonInst->teleportTo(desc.cameraPosition - sunDir * 1000.0f);
            }
        }
    }

    // Skybox の位置をカメラに同期 (フォーカス中のみ)
    if (desc.isFocused) {
        for (auto const& [name, child] : desc.workspace->getChildren()) {
            if (child->IsA("Skybox")) {
                static_cast<BaseCube*>(child.get())->teleportTo(desc.cameraPosition);
            }
        }
    }

    // ---- 素のプリミティブ形状を収集してインスタンス描画リストを作る（シャドウ/メイン共用） ----
    for (int shapeIdx = 0; shapeIdx < INST_SHAPE_COUNT; ++shapeIdx) {
        m_instBatches[shapeIdx].main.clear();
        m_instBatches[shapeIdx].shadow.clear();
    }
    long long instCulled = 0;
    auto collectInstCubes = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        if (inst->IsA("BaseCube")) {
            BaseCube* bc = static_cast<BaseCube*>(inst);
            int shapeIdx = instanceableShapeIndex(bc);
            if (shapeIdx >= 0) {
                CFrame wcf = bc->getWorldCFrame();
                Matrix4 mtx = wcf.toMatrix4() * Matrix4::Scale(bc->Size.x, bc->Size.y, bc->Size.z);
                CubeInstanceData d;
                std::memcpy(d.model, mtx.m, sizeof(d.model));
                d.color[0] = bc->Color.r; d.color[1] = bc->Color.g;
                d.color[2] = bc->Color.b; d.color[3] = bc->Color.a;
                if (bc->CastShadow) m_instBatches[shapeIdx].shadow.push_back(d);
                if (sphereInFrustum(camFrustum, wcf.Position, bc->Size.length() * 0.5f)) {
                    m_instBatches[shapeIdx].main.push_back(d);
                } else {
                    instCulled++;
                }
            }
        }
        for (auto const& [name, child] : inst->getChildren()) self(self, child.get());
    };
    for (auto const& [name, child] : desc.workspace->getChildren()) collectInstCubes(collectInstCubes, child.get());
    if (instCulled > 0) FrameProfiler::get().addCount("cubesCulled", instCulled);

    // ---- Shadow Pass ----
    Matrix4 lightSpaceMatrix;
    bool shadowReady = false;
    if (desc.renderShadows && lighting && shadowFBO && shadowMapTex && depthShader) {
        Vector3 ld = lighting->lightDir;
        float len = std::sqrt(ld.x*ld.x + ld.y*ld.y + ld.z*ld.z);
        if (len > 0.001f) { ld.x /= len; ld.y /= len; ld.z /= len; }
        Vector3 shadowCenter = desc.cameraPosition; // カメラ位置に追従させる(原点固定だと原点から離れると影が消えるため)
        Vector3 lightEye(shadowCenter.x - ld.x * 80.0f, shadowCenter.y - ld.y * 80.0f, shadowCenter.z - ld.z * 80.0f);
        Vector3 upVec = (std::fabsf(ld.y) < 0.99f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
        Matrix4 lightView = Matrix4::LookAt(lightEye, shadowCenter, upVec);
        Matrix4 lightProj = Matrix4::Ortho(-80.0f, 80.0f, -80.0f, 80.0f, 0.1f, 400.0f);
        lightSpaceMatrix = lightProj * lightView;

        FrameProfiler::get().beginSection("shadow");
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        glUseProgram(depthShader);
        static CachedUniform s_lsmDepthLocCache;
        static CachedUniform s_modelDepthLocCache;
        int lsmDepthLoc  = cachedUniformLocation(depthShader, s_lsmDepthLocCache,  "lightSpaceMatrix");
        int modelDepthLoc = cachedUniformLocation(depthShader, s_modelDepthLocCache, "model");
        glUniformMatrix4fv(lsmDepthLoc, 1, GL_FALSE, lightSpaceMatrix.m);

        if (m_uInstancedDepthLoc != -1) {
            bool anyShadowInst = false;
            for (int shapeIdx = 0; shapeIdx < INST_SHAPE_COUNT; ++shapeIdx) {
                const auto& batch = m_instBatches[shapeIdx].shadow;
                if (batch.empty() || instShapes[shapeIdx].vao == 0) continue;
                if (!anyShadowInst) { glUniform1f(m_uInstancedDepthLoc, 1.0f); anyShadowInst = true; }
                glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(batch.size() * sizeof(CubeInstanceData)),
                             batch.data(), GL_STREAM_DRAW);
                glBindVertexArray(instShapes[shapeIdx].vao);
                glDrawElementsInstanced(GL_TRIANGLES, instShapes[shapeIdx].indexCount,
                                        GL_UNSIGNED_INT, 0, (GLsizei)batch.size());
                FrameProfiler::get().addCount("shadowCubes", (long long)batch.size());
            }
            if (anyShadowInst) { glBindVertexArray(0); glUniform1f(m_uInstancedDepthLoc, 0.0f); }
        }

        auto shadowRender = [&](auto& self, Instance* inst) -> void {
            if (!inst) return;
            if (inst->IsA("BaseCube") && instanceableShapeIndex(static_cast<BaseCube*>(inst)) >= 0) {
                // 収集済み → インスタンス描画済み
            } else if (inst->IsA("BaseCube")) {
                BaseCube* bc = static_cast<BaseCube*>(inst);
                if (bc->Color.a > 0.001f && bc->CastShadow) {
                    Matrix4 modelMat = bc->getWorldCFrame().toMatrix4() *
                                       Matrix4::Scale(bc->Size.x, bc->Size.y, bc->Size.z);
                    glUniformMatrix4fv(modelDepthLoc, 1, GL_FALSE, modelMat.m);
                    if (inst->IsA("Cylinder")) {
                        glBindVertexArray(Cylinder::s_VAO);
                        glDrawElements(GL_TRIANGLES, Cylinder::s_IndexCount, GL_UNSIGNED_INT, nullptr);
                    } else if (inst->IsA("TriangularPrism")) {
                        glBindVertexArray(TriangularPrism::s_VAO);
                        glDrawElements(GL_TRIANGLES, TriangularPrism::s_IndexCount, GL_UNSIGNED_INT, nullptr);
                    } else if (inst->IsA("Sphere")) {
                        glBindVertexArray(Sphere::s_VAO);
                        glDrawElements(GL_TRIANGLES, Sphere::s_IndexCount, GL_UNSIGNED_INT, nullptr);
                    } else if (inst->IsA("MeshCube")) {
                        MeshCube* mc = static_cast<MeshCube*>(inst);
                        if (mc->hasGeometry()) {
                            glBindVertexArray(mc->getVAO());
                            glDrawElements(GL_TRIANGLES, mc->getIndexCount(), GL_UNSIGNED_INT, nullptr);
                        }
                    } else {
                        glBindVertexArray(Cube::s_VAO);
                        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
                    }
                    FrameProfiler::get().addCount("shadowCubes", 1);
                }
            }
            for (auto const& [name, child] : inst->getChildren()) {
                self(self, child.get());
            }
        };
        for (auto const& [name, child] : desc.workspace->getChildren()) {
            shadowRender(shadowRender, child.get());
        }

        // ---- Terrain Shadow ----
        Matrix4 identity;
        glUniformMatrix4fv(modelDepthLoc, 1, GL_FALSE, identity.m);
        for (Terrain* terrain : SceneRuntime::collectTerrains(desc.workspace)) {
            if (!terrain->Enabled || !terrain->streamer) continue;
            for (auto& [key, entry] : terrain->streamer->getChunks()) {
                const Chunk& chunk = entry.chunk;
                if (chunk.mesh.indexCount > 0) {
                    glBindVertexArray(chunk.mesh.VAO);
                    glDrawElements(GL_TRIANGLES, (GLsizei)chunk.mesh.indexCount, GL_UNSIGNED_INT, nullptr);
                }
            }
        }
        glBindVertexArray(0);

        shadowReady = true;
        // メインFBOに戻す
        glBindFramebuffer(GL_FRAMEBUFFER, desc.fbo);
        glViewport(0, 0, desc.width, desc.height);
        FrameProfiler::get().endSection("shadow");
    }

    // ---- Main Pass ----
    FrameProfiler::get().beginSection("main");
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(viewLoc,       1, GL_FALSE, view.m);
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, projection.m);
    glUniform3f(viewPosLoc, desc.cameraPosition.x, desc.cameraPosition.y, desc.cameraPosition.z);

    if (lighting) {
        if (lightDirLoc   != -1) glUniform3f(lightDirLoc,   lighting->lightDir.x,  lighting->lightDir.y,  lighting->lightDir.z);
        if (brightnessLoc != -1) glUniform1f(brightnessLoc, lighting->brightness);
        if (lightColorLoc != -1) glUniform3f(lightColorLoc, lighting->lightColor.r, lighting->lightColor.g, lighting->lightColor.b);
    } else {
        if (lightDirLoc   != -1) glUniform3f(lightDirLoc,   1.0f, -1.0f, -1.0f);
        if (brightnessLoc != -1) glUniform1f(brightnessLoc, 1.0f);
        if (lightColorLoc != -1) glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);
    }

    // ---- 追加 Point/Spot 光源を uniform 配列へ ----
    {
        std::vector<LightSource*> lights;
        collectLights(static_cast<Instance*>(desc.workspace), lights);
        int count = 0;
        for (LightSource* ls : lights) {
            if (count >= MAX_LIGHTS) break;
            // 位置（と Spot の向き）は親 Spatial のワールド CFrame から
            Vector3 pos(0.0f, 0.0f, 0.0f);
            Vector3 dir(0.0f, -1.0f, 0.0f);
            if (auto par = ls->Parent.lock()) {
                if (par->IsA("Spatial")) {
                    CFrame cf = static_cast<Spatial*>(par.get())->getWorldCFrame();
                    pos = cf.Position;
                    dir = cf.Rotation.rotate(Vector3(0.0f, -1.0f, 0.0f));  // 親の下向き
                }
            }
            bool isSpot = ls->IsA("SpotLight");
            float cosCutoff = 1.0f;
            if (isSpot) {
                float ang = static_cast<SpotLight*>(ls)->Angle;
                cosCutoff = std::cos(ang * 3.14159265f / 180.0f);
            }
            const LightUniformLocs& loc = lightLocs[count];
            glUniform1i(loc.type, isSpot ? 1 : 0);
            glUniform3f(loc.position,  pos.x, pos.y, pos.z);
            glUniform3f(loc.direction, dir.x, dir.y, dir.z);
            glUniform3f(loc.color, ls->lightColor.r, ls->lightColor.g, ls->lightColor.b);
            glUniform1f(loc.brightness, ls->brightness);
            glUniform1f(loc.range,      ls->range);
            glUniform1f(loc.cosCutoff,  cosCutoff);
            count++;
        }
        glUniform1i(uLightCountLoc, count);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowReady ? shadowMapTex : 0);
    glActiveTexture(GL_TEXTURE0);
    glUniformMatrix4fv(lightSpaceMatrixLoc, 1, GL_FALSE, lightSpaceMatrix.m);
    glUniform1f(hasShadowsLoc, shadowReady ? 1.0f : 0.0f);

    glBindVertexArray(VAO);

    // 波アニメ用の時間と液体フラグ（既定 0）
    const Physics* wavePhysics = desc.workspace->getPhysicsEngine();
    glUniform1f(uTimeLoc, wavePhysics ? wavePhysics->getWaveTime() : 0.0f);
    glUniform1f(uIsLiquidLoc, 0.0f);

    // 完全不透明(Color.a>=1)のオブジェクトはGL_BLENDを無効化して描画コスト削減。
    // init()でglEnable(GL_BLEND)済みのためtrueスタート。ループを抜けた後はこの関数の
    // 後段(renderClouds/renderParticles等)がGL_BLEND常時有効を前提にしているため復元する。
    // また半透明時はglDepthMask(GL_FALSE)でdepth writeも無効化し、深度バッファに
    // 半透明オブジェクトの深度値が書き込まれて後続オブジェクトが深度テストで
    // 弾かれてしまう不具合を防ぐ。
    bool blendEnabled = true;
    bool depthMaskEnabled = true;
    auto setBlendForAlpha = [&](float alpha) {
        bool want = alpha < 0.999f;
        if (want != blendEnabled) {
            if (want) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            blendEnabled = want;
        }
        bool wantDepthWrite = !want;
        if (wantDepthWrite != depthMaskEnabled) {
            glDepthMask(wantDepthWrite ? GL_TRUE : GL_FALSE);
            depthMaskEnabled = wantDepthWrite;
        }
    };

    auto renderInst = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        if (inst->IsA("BaseCube")) {
            BaseCube* bc = static_cast<BaseCube*>(inst);
            if (unlitLoc     != -1) glUniform1f(unlitLoc,     bc->Unlit        ? 1.0f : 0.0f);
            if (triplanarLoc != -1) glUniform1f(triplanarLoc, bc->UseTriplanar ? 1.0f : 0.0f);
            if (texScaleLoc  != -1) glUniform1f(texScaleLoc,  bc->TextureScale);
        }
        if (inst->IsA("Cube")) {
            Cube* cube = static_cast<Cube*>(inst);
            if (instanceableShapeIndex(cube) < 0) {
                if (cube->Color.a > 0.001f) {
                    CFrame wcf = cube->getWorldCFrame();
                    if (sphereInFrustum(camFrustum, wcf.Position, cube->Size.length() * 0.5f)) {
                        Matrix4 m = wcf.toMatrix4() * Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);
                        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                        setBlendForAlpha(cube->Color.a);
                        cube->draw(modelLoc, shaderProgram);
                        FrameProfiler::get().addCount("cubesDrawn", 1);
                    } else {
                        FrameProfiler::get().addCount("cubesCulled", 1);
                    }
                }
            }
        } else if (inst->IsA("Cylinder")) {
            Cylinder* c = static_cast<Cylinder*>(inst);
            if (instanceableShapeIndex(c) < 0) {
                if (c->Color.a > 0.001f) {
                    CFrame wcf = c->getWorldCFrame();
                    if (sphereInFrustum(camFrustum, wcf.Position, c->Size.length() * 0.5f)) {
                        Matrix4 m = wcf.toMatrix4() * Matrix4::Scale(c->Size.x, c->Size.y, c->Size.z);
                        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                        setBlendForAlpha(c->Color.a);
                        c->draw(modelLoc, shaderProgram);
                        FrameProfiler::get().addCount("cubesDrawn", 1);
                    } else {
                        FrameProfiler::get().addCount("cubesCulled", 1);
                    }
                }
            }
        } else if (inst->IsA("TriangularPrism")) {
            TriangularPrism* tp = static_cast<TriangularPrism*>(inst);
            if (instanceableShapeIndex(tp) < 0) {
                if (tp->Color.a > 0.001f) {
                    CFrame wcf = tp->getWorldCFrame();
                    if (sphereInFrustum(camFrustum, wcf.Position, tp->Size.length() * 0.5f)) {
                        Matrix4 m = wcf.toMatrix4() * Matrix4::Scale(tp->Size.x, tp->Size.y, tp->Size.z);
                        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                        setBlendForAlpha(tp->Color.a);
                        tp->draw(modelLoc, shaderProgram);
                        FrameProfiler::get().addCount("cubesDrawn", 1);
                    } else {
                        FrameProfiler::get().addCount("cubesCulled", 1);
                    }
                }
            }
        } else if (inst->IsA("Sphere")) {
            Sphere* sp = static_cast<Sphere*>(inst);
            if (instanceableShapeIndex(sp) < 0) {
                if (sp->Color.a > 0.001f) {
                    CFrame wcf = sp->getWorldCFrame();
                    if (sphereInFrustum(camFrustum, wcf.Position, sp->Size.length() * 0.5f)) {
                        Matrix4 m = wcf.toMatrix4() * Matrix4::Scale(sp->Size.x, sp->Size.y, sp->Size.z);
                        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                        setBlendForAlpha(sp->Color.a);
                        sp->draw(modelLoc, shaderProgram);
                        FrameProfiler::get().addCount("cubesDrawn", 1);
                    } else {
                        FrameProfiler::get().addCount("cubesCulled", 1);
                    }
                }
            }
        } else if (inst->IsA("MeshCube")) {
            MeshCube* mc = static_cast<MeshCube*>(inst);
            if (mc->Color.a > 0.001f) {
                CFrame wcf = mc->getWorldCFrame();
                if (sphereInFrustum(camFrustum, wcf.Position, mc->Size.length() * 0.5f)) {
                    Matrix4 m = wcf.toMatrix4() * Matrix4::Scale(mc->Size.x, mc->Size.y, mc->Size.z);
                    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                    setBlendForAlpha(mc->Color.a);
                    mc->draw(modelLoc, shaderProgram);
                    FrameProfiler::get().addCount("cubesDrawn", 1);
                } else {
                    FrameProfiler::get().addCount("cubesCulled", 1);
                }
            }
        } else if (inst->IsA("LiquidCube")) {
            LiquidCube* lc = static_cast<LiquidCube*>(inst);
            if (lc->Color.a > 0.001f) {
                CFrame wcf = lc->getWorldCFrame();
                if (sphereInFrustum(camFrustum, wcf.Position, lc->Size.length() * 0.5f)) {
                    glUniform1f(uIsLiquidLoc, 1.0f);
                    Matrix4 m = wcf.toMatrix4() * Matrix4::Scale(lc->Size.x, lc->Size.y, lc->Size.z);
                    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                    setBlendForAlpha(lc->Color.a);
                    lc->draw(modelLoc, shaderProgram);
                    glUniform1f(uIsLiquidLoc, 0.0f);
                    FrameProfiler::get().addCount("cubesDrawn", 1);
                } else {
                    FrameProfiler::get().addCount("cubesCulled", 1);
                }
            }
        }
        for (auto const& [name, child] : inst->getChildren()) {
            self(self, child.get());
        }
    };

    // ---- 素のプリミティブ形状のインスタンス一括描画（不透明なので最初に描く） ----
    {
        bool anyMainInst = false;
        for (int shapeIdx = 0; shapeIdx < INST_SHAPE_COUNT; ++shapeIdx) {
            if (!m_instBatches[shapeIdx].main.empty() && instShapes[shapeIdx].vao != 0) { anyMainInst = true; break; }
        }
        if (anyMainInst && m_uInstancedLoc != -1) {
            setBlendForAlpha(1.0f); // blend無効・depth write有効
            if (unlitLoc          != -1) glUniform1f(unlitLoc,          0.0f);
            if (triplanarLoc      != -1) glUniform1f(triplanarLoc,      0.0f);
            if (texScaleLoc       != -1) glUniform1f(texScaleLoc,       1.0f);
            if (useVertexColorLoc != -1) glUniform1f(useVertexColorLoc, 0.0f);
            static CachedUniform s_instUvScaleCache;
            static CachedUniform s_instIsSurfaceGuiCache;
            int instUvScaleLoc      = cachedUniformLocation(shaderProgram, s_instUvScaleCache,      "uvScale");
            int instIsSurfaceGuiLoc = cachedUniformLocation(shaderProgram, s_instIsSurfaceGuiCache, "isSurfaceGui");
            if (instUvScaleLoc      != -1) glUniform2f(instUvScaleLoc, 1.0f, 1.0f);
            if (instIsSurfaceGuiLoc != -1) glUniform1f(instIsSurfaceGuiLoc, 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, Cube::defaultTextureID);
            glUniform1f(m_uInstancedLoc, 1.0f);
            for (int shapeIdx = 0; shapeIdx < INST_SHAPE_COUNT; ++shapeIdx) {
                const auto& batch = m_instBatches[shapeIdx].main;
                if (batch.empty() || instShapes[shapeIdx].vao == 0) continue;
                glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(batch.size() * sizeof(CubeInstanceData)),
                             batch.data(), GL_STREAM_DRAW);
                glBindVertexArray(instShapes[shapeIdx].vao);
                glDrawElementsInstanced(GL_TRIANGLES, instShapes[shapeIdx].indexCount,
                                        GL_UNSIGNED_INT, 0, (GLsizei)batch.size());
                FrameProfiler::get().addCount("cubesDrawn", (long long)batch.size());
                FrameProfiler::get().addCount("instanced",  (long long)batch.size());
            }
            glUniform1f(m_uInstancedLoc, 0.0f);
            glBindVertexArray(VAO); // 以降の個別描画は既存VAO前提
        }
    }

    for (auto const& [name, child] : desc.workspace->getChildren()) {
        renderInst(renderInst, child.get());
    }

    // renderClouds/renderParticles等はGL_BLENDが常時有効という前提のため復元する
    if (!blendEnabled) { glEnable(GL_BLEND); blendEnabled = true; }
    if (!depthMaskEnabled) { glDepthMask(GL_TRUE); depthMaskEnabled = true; }
    FrameProfiler::get().endSection("main");

    FrameProfiler::get().beginSection("extras");
    // ---- Editor選択外枠。Highlightインスタンスの塗り設定とは独立 ----
    if (desc.renderHighlights && desc.primarySelection) {
        static const Color4 kTransparentFill(0.0f, 0.0f, 0.0f, 0.0f);
        static const Color4 kPrimaryOutline(1.0f, 1.0f, 0.0f, 1.0f);
        static const Color4 kSecondaryOutline(1.0f, 0.59f, 0.12f, 0.82f);
        constexpr float kSelectionOutlineThickness = 2.0f;

        std::unordered_set<BaseCube*> secondaryDrawn;
        if (desc.selectionTargets) {
            for (Instance* selected : *desc.selectionTargets) {
                if (!selected || selected == desc.primarySelection || selected->Parent.expired()) continue;
                std::vector<BaseCube*> targets;
                collectHighlightTargets(selected, targets);
                for (BaseCube* bc : targets) {
                    if (!bc || !secondaryDrawn.insert(bc).second) continue;
                    drawBaseCubeHighlight(
                        bc, kTransparentFill, kSecondaryOutline, kSelectionOutlineThickness,
                        view, projection, desc.cameraPosition, fovYDegrees, desc.height);
                }
            }
        }

        if (!desc.primarySelection->Parent.expired()) {
            std::vector<BaseCube*> primaryTargets;
            collectHighlightTargets(desc.primarySelection, primaryTargets);
            for (BaseCube* bc : primaryTargets) {
                drawBaseCubeHighlight(
                    bc, kTransparentFill, kPrimaryOutline, kSelectionOutlineThickness,
                    view, projection, desc.cameraPosition, fovYDegrees, desc.height);
            }
        }
    }

    // ---- Highlightインスタンス（ゲームプレイ機能。エディタ有無に関わらず描画） ----
    if (desc.renderInstanceHighlights) {
        renderInstanceHighlights(*desc.workspace, view, projection, desc.cameraPosition, fovYDegrees, desc.height);
    }

    // ---- 制約ビジュアライズ（Rope/Rod） ----
    if (desc.renderConstraints) {
        renderConstraints(*desc.workspace, view, projection, desc.cameraPosition);
    }

    // ---- 物理制約デバッグビジュアライザー（Weld/Motor/Attachment/Force。デフォルトOFF） ----
    if (desc.renderPhysicsDebug) {
        renderPhysicsDebug(*desc.workspace, view, projection, desc.cameraPosition);
    }

    // ---- Terrain の描画 ----
    // ---- Terrain の描画 ----
    renderTerrain(view, projection, desc.workspace);

    // ---- 雲の描画（Weather。状態更新はメインループ側で毎フレーム1回のみ実行済み） ----
    renderClouds(*desc.workspace, view, projection, desc.cameraPosition);

    // ---- 雷柱の描画（Weather。ジオメトリはWeather::attemptStrike()側で生成済み） ----
    renderLightning(*desc.workspace, view, projection, desc.cameraPosition);

    // ---- パーティクル描画（シミュレーションはメインループ側で毎フレーム1回のみ実行済み） ----
    {
        Vector3 pRight = Vector3::Cross(desc.cameraForward, desc.cameraUp).normalize();
        Vector3 pUp    = Vector3::Cross(pRight, desc.cameraForward).normalize();
        renderParticles(*desc.workspace, view, projection, pRight, pUp);
    }

    // ---- ポストエフェクト（PostEffect の ZIndex 順チェーン適用） ----
    if (desc.renderPostEffects) {
        renderPostEffects(*desc.workspace, desc.fbo, desc.width, desc.height);
    }
    FrameProfiler::get().endSection("extras");

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    
    // GUI 描画のためにビュー/プロジェクション行列を保存 (Primary Viewport用と仮定)
    if (desc.renderHighlights) { 
        m_lastView = view;
        m_lastProj = projection;
    }
}

// ===================================================
//  メインループから呼ぶ統合描画
// ===================================================
void Renderer::render(User& user, GLFWwindow* window, Workspace& workspace) {
    // Primary Viewport用の描画（スタンドアロンまたはエディターのメインビュー）
    // シーンを editor 側が描くのは ownsSceneRender()==true の実エディターのみ
    // (ViewportPanel が描き直すため、ここで描くと無駄な二重描画になる)。
    // editor が無い場合と、GUIしか描かない NullEditorManager(ランタイム)の場合はここで描画する。
    if (!editor || !editor->ownsSceneRender()) {
        ViewportRenderDesc desc;
        desc.workspace = &workspace;
        desc.cameraPosition = user.cpos;
        desc.cameraForward  = user.forward;
        desc.cameraUp       = user.up;
        desc.renderShadows = true;
        desc.renderConstraints = true;

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        desc.fbo = 0;
        desc.renderHighlights = false;
        desc.isFocused = true; // スタンドアロンの場合は常にフォーカスされているとみなす

        desc.width = width;
        desc.height = height > 0 ? height : 1;

        // Viewport描画を実行
        renderViewport(desc);
    }

    if (editor) {
        // 既定のフレームバッファをクリア（ImGui 用）
        editor->clearForImGui(window);
        // ImGui フレーム描画
        FrameProfiler::get().beginSection("ui");
        editor->renderUI(user, window, workspace);
        FrameProfiler::get().endSection("ui");
    }

    FrameProfiler::get().beginSection("swap");
    glfwSwapBuffers(window);
    FrameProfiler::get().endSection("swap");
    glfwPollEvents();
}

// ===================================================
//  ImGui フレーム
// ===================================================
void Renderer::renderImGui(User& user, GLFWwindow* window, Workspace& workspace) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // EditorManager が全パネルを描画
    if (editor) {
        editor->render(window);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
}

// ===================================================
//  テクスチャ読み込み
// ===================================================
unsigned int Renderer::loadTexture(const char* path) {
    std::string pathStr(path);
    if (textureCache.find(pathStr) != textureCache.end()) {
        return textureCache[pathStr];
    }
    if (!AssetGuard::allow(pathStr)) return 0;

    int width, height, nrChannels;
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 4);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    unsigned int textureID = 0;
    if (!data) {
        RCBN_WARN("Failed to load texture: " << path);
        return 0;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    std::cout << "Success: " << path << " (" << nrChannels << "ch)" << std::endl;
    stbi_image_free(data);

    glBindTexture(GL_TEXTURE_2D, 0);  // texture state をクリアして後の描画に影響しないようにする
    textureCache[pathStr] = textureID;
    return textureID;
}

// GLB等のメモリ上のバイト列からテクスチャを読み込む(パスキーが無いためキャッシュはしない)
unsigned int Renderer::loadTextureFromMemory(const unsigned char* data, size_t size) {
    int width, height, nrChannels;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &nrChannels, 4);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (!pixels) {
        RCBN_WARN("Failed to load texture from memory");
        return 0;
    }

    unsigned int textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    stbi_image_free(pixels);
    glBindTexture(GL_TEXTURE_2D, 0);  // texture state をクリアして後の描画に影響しないようにする
    return textureID;
}

// ---------------------------------------------------
//  renderTerrain
// ---------------------------------------------------
void Renderer::renderTerrain(const Matrix4& view, const Matrix4& projection, Workspace* workspace)
{
    if (!workspace) return;

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(viewLoc,       1, GL_FALSE, view.m);
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, projection.m);

    Matrix4 identity;
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, identity.m);

    if (unlitLoc          != -1) glUniform1f(unlitLoc,          0.0f);
    if (triplanarLoc      != -1) glUniform1f(triplanarLoc,      0.0f);
    if (texScaleLoc       != -1) glUniform1f(texScaleLoc,       1.0f);
    if (useVertexColorLoc != -1) glUniform1f(useVertexColorLoc, 1.0f);
    if (ourColorLoc       != -1) glUniform4f(ourColorLoc,       1.0f, 1.0f, 1.0f, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);

    for (Terrain* terrain : SceneRuntime::collectTerrains(workspace)) {
        if (!terrain || !terrain->Enabled || !terrain->streamer) continue;
        for (auto& [key, entry] : terrain->streamer->getChunks()) {
            const Chunk& chunk = entry.chunk;
            if (chunk.mesh.indexCount == 0) continue;
            glBindVertexArray(chunk.mesh.VAO);
            glDrawElements(GL_TRIANGLES, (GLsizei)chunk.mesh.indexCount,
                           GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindVertexArray(0);
    if (useVertexColorLoc != -1) glUniform1f(useVertexColorLoc, 0.0f);
}
