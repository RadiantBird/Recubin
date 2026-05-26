#include <Core/Renderer.hpp>
#include <Core/FileLoader.hpp>
#include <Util/Logger.hpp>
#include <Editor/IEditorManager.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "include/stb_image.h"

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

// ===================================================
//  init
// ===================================================
void Renderer::init(GLFWwindow* window) {
    instance  = this;
    m_window  = window;

#ifndef EDITOR_DISABLED
    // ImGui 初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    io.Fonts->AddFontFromFileTTF("assets/fonts/DotGothic16-Regular.ttf", 22.0f, nullptr,
                                  io.Fonts->GetGlyphRangesJapanese());

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

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

    int lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");
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
}

// ===================================================
//  デストラクタ
// ===================================================
Renderer::~Renderer() {
    if (instance == this) instance = nullptr;

#ifndef EDITOR_DISABLED
    editor.reset(); // EditorManager を先に破棄（FBO が ImGui より先に解放される）

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif

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

        if (inst->GetClassName() == "Rope") {
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
        } else if (inst->GetClassName() == "Rod") {
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
