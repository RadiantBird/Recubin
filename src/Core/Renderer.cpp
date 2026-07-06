#include <Core/Renderer.hpp>
#include <Core/FileLoader.hpp>
#include <Util/Logger.hpp>
#include <Util/AssetGuard.hpp>
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
#include <Instances/Weather.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <include/Math/PerlinNoise.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>
#include <include/Instances/PostEffect.hpp>
#include <algorithm>
#include <filesystem>


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
    unsigned char white[] = { 255, 255, 255, 255 };
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

    std::string vShaderStr = FileLoader::readText("src/vertex.glsl");
    std::string fShaderStr = FileLoader::readText("src/fragment.glsl");
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

    GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, U));
    glEnableVertexAttribArray(2);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");
    glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);
    lightDirLoc   = glGetUniformLocation(shaderProgram, "lightDir");
    brightnessLoc = glGetUniformLocation(shaderProgram, "brightness");
    glUniform3f(lightDirLoc, 1.0f, -1.0f, -1.0f);
    glUniform1f(brightnessLoc, 1.0f);



    // --- Shadow map テクスチャユニットのバインド設定 ---
    glUniform1i(glGetUniformLocation(shaderProgram, "shadowMap"), 1);
    glUniform1f(glGetUniformLocation(shaderProgram, "hasShadows"), 0.0f);

    // --- Depth シェーダーのコンパイル（Shadow Pass 用）---
    {
        std::string dvStr = FileLoader::readText("src/depth_vertex.glsl");
        std::string dfStr = FileLoader::readText("src/depth_fragment.glsl");
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

void Renderer::renderLightning(Workspace& workspace, const Matrix4& view, const Matrix4& projection) {
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

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glUniform4f(glGetUniformLocation(m_lineShader, "lineColor"), 0.9f, 0.95f, 1.0f, alpha);

    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizei)(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
    glLineWidth(3.0f);
    glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(verts.size() / 3));
    glLineWidth(1.0f);
    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

void Renderer::renderConstraints(Workspace& workspace, const Matrix4& view, const Matrix4& projection) {
    if (!m_lineShader) return;

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glBindVertexArray(m_lineVAO);

    int colorLoc = glGetUniformLocation(m_lineShader, "lineColor");

    auto uploadAndDraw = [&](const std::vector<float>& verts) {
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizei)(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(verts.size() / 3));
    };

    auto scan = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;

        if (inst->getClassName() == "Rope") {
            Rope* rope = static_cast<Rope*>(inst);
            auto c0 = rope->m_cube0.lock();
            auto c1 = rope->m_cube1.lock();
            if (c0 && c1) {
                Vector3 p0 = c0->getWorldCFrame().Position;
                Vector3 p1 = c1->getWorldCFrame().Position;
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
                glLineWidth(rope->LineWidth);
                glUniform4f(colorLoc, rope->Color.r, rope->Color.g, rope->Color.b, rope->Color.a);
                uploadAndDraw(verts);
            }
        } else if (inst->getClassName() == "Rod") {
            Rod* rod = static_cast<Rod*>(inst);
            auto c0 = rod->m_cube0.lock();
            auto c1 = rod->m_cube1.lock();
            if (c0 && c1) {
                Vector3 p0 = c0->getWorldCFrame().Position;
                Vector3 p1 = c1->getWorldCFrame().Position;
                std::vector<float> verts = {
                    p0.x, p0.y, p0.z,
                    p1.x, p1.y, p1.z
                };
                glLineWidth(rod->LineWidth);
                glUniform4f(colorLoc, rod->Color.r, rod->Color.g, rod->Color.b, rod->Color.a);
                uploadAndDraw(verts);
            }
        }

        for (auto const& [name, child] : inst->getChildren()) {
            self(self, child.get());
        }
    };

    for (auto const& [name, child] : workspace.getChildren()) {
        scan(scan, child.get());
    }

    glLineWidth(1.0f);
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
//  地形ブラシのヒット位置ガイド（水平リング）
// ===================================================
void Renderer::renderBrushMarker(const Matrix4& view, const Matrix4& projection,
                                 const Vector3& center, float radius) {
    if (!m_lineShader || radius <= 0.0f) return;

    glUseProgram(m_lineShader);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "projection"), 1, GL_FALSE, projection.m);
    glUniform4f(glGetUniformLocation(m_lineShader, "lineColor"), 1.0f, 0.85f, 0.2f, 1.0f);

    constexpr int SEG = 48;
    std::vector<float> verts;
    verts.reserve((SEG + 1) * 3);
    for (int i = 0; i <= SEG; i++) {
        float a = (float)i / (float)SEG * 6.28318530718f;
        verts.push_back(center.x + std::cos(a) * radius);
        verts.push_back(center.y + 0.15f); // 地表とのZファイト回避に少し浮かせる
        verts.push_back(center.z + std::sin(a) * radius);
    }

    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizei)(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(verts.size() / 3));
    glLineWidth(1.0f);
    glBindVertexArray(0);
    glUseProgram(shaderProgram);
}

// ===================================================
//  ポストエフェクト（PostEffect インスタンスの ZIndex 順チェーン適用）
// ===================================================
void Renderer::initPostEffectRenderer() {
    std::string vStr = FileLoader::readText("src/postprocess_vertex.glsl");
    std::string fStr = FileLoader::readText("src/postprocess_fragment.glsl");
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
    Matrix4 projection = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
    Matrix4 view       = Matrix4::LookAt(desc.cameraPosition, desc.cameraPosition + desc.cameraForward, desc.cameraUp);

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

    // ---- Shadow Pass ----
    Matrix4 lightSpaceMatrix;
    bool shadowReady = false;
    if (desc.renderShadows && lighting && shadowFBO && shadowMapTex && depthShader) {
        Vector3 ld = lighting->lightDir;
        float len = std::sqrt(ld.x*ld.x + ld.y*ld.y + ld.z*ld.z);
        if (len > 0.001f) { ld.x /= len; ld.y /= len; ld.z /= len; }
        Vector3 lightEye(-ld.x * 80.0f, -ld.y * 80.0f, -ld.z * 80.0f);
        Vector3 upVec = (std::fabsf(ld.y) < 0.99f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
        Matrix4 lightView = Matrix4::LookAt(lightEye, Vector3(0.0f, 0.0f, 0.0f), upVec);
        Matrix4 lightProj = Matrix4::Ortho(-80.0f, 80.0f, -80.0f, 80.0f, 0.1f, 400.0f);
        lightSpaceMatrix = lightProj * lightView;

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        glUseProgram(depthShader);
        int lsmDepthLoc  = glGetUniformLocation(depthShader, "lightSpaceMatrix");
        int modelDepthLoc = glGetUniformLocation(depthShader, "model");
        glUniformMatrix4fv(lsmDepthLoc, 1, GL_FALSE, lightSpaceMatrix.m);

        auto shadowRender = [&](auto& self, Instance* inst) -> void {
            if (!inst) return;
            if (inst->IsA("BaseCube")) {
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
        for (auto const& [name, child] : desc.workspace->getChildren()) {
            if (!child->IsA("Terrain")) continue;
            auto* terrain = static_cast<Terrain*>(child.get());
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
    }

    // ---- Main Pass ----
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection.m);
    glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), desc.cameraPosition.x, desc.cameraPosition.y, desc.cameraPosition.z);
    
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
        constexpr int MAX_LIGHTS = 8;
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
            std::string b = "uLights[" + std::to_string(count) + "].";
            glUniform1i(glGetUniformLocation(shaderProgram, (b + "type").c_str()), isSpot ? 1 : 0);
            glUniform3f(glGetUniformLocation(shaderProgram, (b + "position").c_str()),  pos.x, pos.y, pos.z);
            glUniform3f(glGetUniformLocation(shaderProgram, (b + "direction").c_str()), dir.x, dir.y, dir.z);
            glUniform3f(glGetUniformLocation(shaderProgram, (b + "color").c_str()),
                        ls->lightColor.r, ls->lightColor.g, ls->lightColor.b);
            glUniform1f(glGetUniformLocation(shaderProgram, (b + "brightness").c_str()), ls->brightness);
            glUniform1f(glGetUniformLocation(shaderProgram, (b + "range").c_str()),      ls->range);
            glUniform1f(glGetUniformLocation(shaderProgram, (b + "cosCutoff").c_str()),  cosCutoff);
            count++;
        }
        glUniform1i(glGetUniformLocation(shaderProgram, "uLightCount"), count);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowReady ? shadowMapTex : 0);
    glActiveTexture(GL_TEXTURE0);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "lightSpaceMatrix"), 1, GL_FALSE, lightSpaceMatrix.m);
    glUniform1f(glGetUniformLocation(shaderProgram, "hasShadows"), shadowReady ? 1.0f : 0.0f);

    int modelLoc        = glGetUniformLocation(shaderProgram, "model");
    int unlitLoc        = glGetUniformLocation(shaderProgram, "unlit");
    int triplanarLoc    = glGetUniformLocation(shaderProgram, "useTriplanar");
    int texScaleLoc     = glGetUniformLocation(shaderProgram, "u_textureScale");
    glBindVertexArray(VAO);

    // 波アニメ用の時間と液体フラグ（既定 0）
    glUniform1f(glGetUniformLocation(shaderProgram, "uTime"),     (float)glfwGetTime());
    glUniform1f(glGetUniformLocation(shaderProgram, "uIsLiquid"), 0.0f);

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
            if (cube->Color.a > 0.001f) {
                Matrix4 m = cube->getWorldCFrame().toMatrix4() * Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                cube->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("Cylinder")) {
            Cylinder* c = static_cast<Cylinder*>(inst);
            if (c->Color.a > 0.001f) {
                Matrix4 m = c->getWorldCFrame().toMatrix4() * Matrix4::Scale(c->Size.x, c->Size.y, c->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                c->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("TriangularPrism")) {
            TriangularPrism* tp = static_cast<TriangularPrism*>(inst);
            if (tp->Color.a > 0.001f) {
                Matrix4 m = tp->getWorldCFrame().toMatrix4() * Matrix4::Scale(tp->Size.x, tp->Size.y, tp->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                tp->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("Sphere")) {
            Sphere* sp = static_cast<Sphere*>(inst);
            if (sp->Color.a > 0.001f) {
                Matrix4 m = sp->getWorldCFrame().toMatrix4() * Matrix4::Scale(sp->Size.x, sp->Size.y, sp->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                sp->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("MeshCube")) {
            MeshCube* mc = static_cast<MeshCube*>(inst);
            if (mc->Color.a > 0.001f) {
                Matrix4 m = mc->getWorldCFrame().toMatrix4() * Matrix4::Scale(mc->Size.x, mc->Size.y, mc->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                mc->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("LiquidCube")) {
            LiquidCube* lc = static_cast<LiquidCube*>(inst);
            if (lc->Color.a > 0.001f) {
                glUniform1f(glGetUniformLocation(shaderProgram, "uIsLiquid"), 1.0f);
                Matrix4 m = lc->getWorldCFrame().toMatrix4() * Matrix4::Scale(lc->Size.x, lc->Size.y, lc->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, m.m);
                lc->draw(modelLoc, shaderProgram);
                glUniform1f(glGetUniformLocation(shaderProgram, "uIsLiquid"), 0.0f);
            }
        }
        for (auto const& [name, child] : inst->getChildren()) {
            self(self, child.get());
        }
    };

    for (auto const& [name, child] : desc.workspace->getChildren()) {
        renderInst(renderInst, child.get());
    }

    // ---- 選択インスタンスの黄色ワイヤーフレームハイライト ----
    if (desc.renderHighlights && editor) {
        if (Instance* sel = editor->getSelectedInstance()) {
            if (!sel->Parent.expired() && sel->IsA("BaseCube")) {
                BaseCube* bc = static_cast<BaseCube*>(sel);
                Matrix4 modelMat = bc->getWorldCFrame().toMatrix4() *
                                   Matrix4::Scale(bc->Size.x * 1.02f, bc->Size.y * 1.02f, bc->Size.z * 1.02f);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
                int colorLocHl = glGetUniformLocation(shaderProgram, "ourColor");
                if (colorLocHl != -1) glUniform4f(colorLocHl, 1.0f, 1.0f, 0.0f, 1.0f);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, whiteTexture);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(2.0f);
                if (sel->IsA("Cylinder")) {
                    glBindVertexArray(Cylinder::s_VAO);
                    glDrawElements(GL_TRIANGLES, Cylinder::s_IndexCount, GL_UNSIGNED_INT, nullptr);
                } else if (sel->IsA("TriangularPrism")) {
                    glBindVertexArray(TriangularPrism::s_VAO);
                    glDrawElements(GL_TRIANGLES, TriangularPrism::s_IndexCount, GL_UNSIGNED_INT, nullptr);
                } else if (sel->IsA("Sphere")) {
                    glBindVertexArray(Sphere::s_VAO);
                    glDrawElements(GL_TRIANGLES, Sphere::s_IndexCount, GL_UNSIGNED_INT, nullptr);
                } else if (sel->IsA("MeshCube")) {
                    MeshCube* mc = static_cast<MeshCube*>(sel);
                    if (mc->hasGeometry()) {
                        glBindVertexArray(mc->getVAO());
                        glDrawElements(GL_TRIANGLES, mc->getIndexCount(), GL_UNSIGNED_INT, nullptr);
                    }
                } else {
                    glBindVertexArray(Cube::s_VAO);
                    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
                }
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glLineWidth(1.0f);
            }
        }
    }

    // ---- 制約ビジュアライズ（Rope/Rod） ----
    if (desc.renderConstraints) {
        renderConstraints(*desc.workspace, view, projection);
    }

    // ---- Terrain の描画 ----
    // ---- Terrain の描画 ----
    renderTerrain(view, projection, desc.workspace);

    // ---- 雲の描画（Weather。状態更新はメインループ側で毎フレーム1回のみ実行済み） ----
    renderClouds(*desc.workspace, view, projection, desc.cameraPosition);

    // ---- 雷柱の描画（Weather。ジオメトリはWeather::attemptStrike()側で生成済み） ----
    renderLightning(*desc.workspace, view, projection);

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
    ViewportRenderDesc desc;
    desc.workspace = &workspace;
    desc.cameraPosition = user.cpos;
    desc.cameraForward  = user.forward;
    desc.cameraUp       = user.up;
    desc.renderShadows = true;
    desc.renderConstraints = true;

    int width, height;
    if (editor) {
        editor->getViewportSize(window, width, height);
        desc.fbo = editor->getViewportFBO();
        desc.renderHighlights = true;
        desc.isFocused = editor->isViewportFocused();
    } else {
        glfwGetFramebufferSize(window, &width, &height);
        desc.fbo = 0;
        desc.renderHighlights = false;
        desc.isFocused = true; // スタンドアロンの場合は常にフォーカスされているとみなす
    }
    
    desc.width = width;
    desc.height = height > 0 ? height : 1;

    // Viewport描画を実行
    // EditorManagerがFBOをバインドしている場合は、renderViewport内で正しく上書き・復元される
    renderViewport(desc);

    if (editor) {
        // 既定のフレームバッファをクリア（ImGui 用）
        editor->clearForImGui(window);
        // ImGui フレーム描画
        editor->renderUI(user, window, workspace);
    }

    glfwSwapBuffers(window);
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
    return textureID;
}

// ---------------------------------------------------
//  renderTerrain
// ---------------------------------------------------
void Renderer::renderTerrain(const Matrix4& view, const Matrix4& projection, Workspace* workspace)
{
    if (!workspace) return;

    // Workspaceツリーにある Terrain を探す
    Terrain* terrain = nullptr;
    for (auto& [name, child] : workspace->getChildren()) {
        if (child->IsA("Terrain")) {
            terrain = static_cast<Terrain*>(child.get());
            break;
        }
    }
    if (!terrain || !terrain->Enabled || !terrain->streamer) return;

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection.m);

    Matrix4 identity;
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, identity.m);

    int unlitLoc        = glGetUniformLocation(shaderProgram, "unlit");
    int triplanarLoc    = glGetUniformLocation(shaderProgram, "useTriplanar");
    int texScaleLoc     = glGetUniformLocation(shaderProgram, "u_textureScale");
    int useVertexColLoc = glGetUniformLocation(shaderProgram, "useVertexColor");
    int ourColorLoc     = glGetUniformLocation(shaderProgram, "ourColor");
    if (unlitLoc        != -1) glUniform1f(unlitLoc,        0.0f);
    if (triplanarLoc    != -1) glUniform1f(triplanarLoc,    0.0f);
    if (texScaleLoc     != -1) glUniform1f(texScaleLoc,     1.0f);
    if (useVertexColLoc != -1) glUniform1f(useVertexColLoc, 1.0f);
    if (ourColorLoc     != -1) glUniform4f(ourColorLoc,     1.0f, 1.0f, 1.0f, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);

    for (auto& [key, entry] : terrain->streamer->getChunks()) {
        const Chunk& chunk = entry.chunk;
        if (chunk.mesh.indexCount == 0) continue;
        glBindVertexArray(chunk.mesh.VAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)chunk.mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    if (useVertexColLoc != -1) glUniform1f(useVertexColLoc, 0.0f);
}
