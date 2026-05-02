#include <Core/Renderer.hpp>
#include <Core/FileLoader.hpp>
#include <Editor/EditorManager.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Lighting.hpp>

#define STB_IMAGE_IMPLEMENTATION
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

static bool lightingHasSkyboxSource(const Lighting* lighting) {
    if (!lighting) return false;
    if (lighting->cubemapTexture != 0 || lighting->skyboxDirty) return true;
    for (int i = 0; i < 6; i++) {
        if (!lighting->skyboxPaths[i].empty()) return true;
    }
    return false;
}

static Lighting* findLightingInTree(Instance* inst) {
    if (!inst) return nullptr;
    Lighting* fallback = nullptr;
    if (inst->IsA("Lighting")) {
        Lighting* lighting = static_cast<Lighting*>(inst);
        fallback = lighting;
        if (lightingHasSkyboxSource(lighting)) return lighting;
    }
    for (auto& [name, child] : inst->getChildren()) {
        Lighting* found = findLightingInTree(child.get());
        if (found) {
            if (lightingHasSkyboxSource(found)) return found;
            if (!fallback) fallback = found;
        }
    }
    return fallback;
}

// ===================================================
//  init
// ===================================================
void Renderer::init(GLFWwindow* window) {
    instance = this;

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

    // --- Skybox シェーダーのコンパイル ---
    {
        std::string svStr = FileLoader::readText("src/skybox_vertex.glsl");
        std::string sfStr = FileLoader::readText("src/skybox_fragment.glsl");
        const char* svSrc = svStr.c_str();
        const char* sfSrc = sfStr.c_str();

        unsigned int sv = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(sv, 1, &svSrc, NULL);
        glCompileShader(sv);
        int ok; char log[512];
        glGetShaderiv(sv, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(sv, 512, NULL, log); std::cout << "SKYBOX_VERT: " << log << std::endl; }

        unsigned int sf = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(sf, 1, &sfSrc, NULL);
        glCompileShader(sf);
        glGetShaderiv(sf, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(sf, 512, NULL, log); std::cout << "SKYBOX_FRAG: " << log << std::endl; }

        skyboxShader = glCreateProgram();
        glAttachShader(skyboxShader, sv);
        glAttachShader(skyboxShader, sf);
        glLinkProgram(skyboxShader);
        glDeleteShader(sv);
        glDeleteShader(sf);

        glUseProgram(skyboxShader);
        glUniform1i(glGetUniformLocation(skyboxShader, "skybox"), 0);
    }

    // --- Skybox VAO（位置のみの単位キューブ）---
    {
        float skyboxVertices[] = {
            -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
        };
        glGenVertexArrays(1, &skyboxVAO);
        glGenBuffers(1, &skyboxVBO);
        glBindVertexArray(skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

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
}

// ===================================================
//  デストラクタ
// ===================================================
Renderer::~Renderer() {
    if (instance == this) instance = nullptr;

    editor.reset(); // EditorManager を先に破棄（FBO が ImGui より先に解放される）

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &VAO);
    glDeleteProgram(shaderProgram);
    if (skyboxVAO)    glDeleteVertexArrays(1, &skyboxVAO);
    if (skyboxVBO)    glDeleteBuffers(1, &skyboxVBO);
    if (skyboxShader) glDeleteProgram(skyboxShader);
    if (shadowFBO)    glDeleteFramebuffers(1, &shadowFBO);
    if (shadowMapTex) glDeleteTextures(1, &shadowMapTex);
    if (depthShader)  glDeleteProgram(depthShader);
}

// ===================================================
//  3D シーン描画（FBO または既定の FB に描く）
// ===================================================
void Renderer::renderScene(User& user, Workspace& workspace) {
    int width  = editor ? editor->viewportPanel->fbWidth  : 800;
    int height = editor ? editor->viewportPanel->fbHeight : 600;
    if (height == 0) height = 1;

    glViewport(0, 0, width, height);

    float aspect = (float)width / (float)height;
    Matrix4 projection = Matrix4::Perspective(45.0f, aspect, 0.1f, 10000.0f);
    Vector3 target = user.cpos + user.forward;
    Matrix4 view   = Matrix4::LookAt(user.cpos, target, user.up);

    // System（Workspace の親）から Lighting を探し、uniform を更新
    Lighting* lighting = nullptr;
    {
        auto systemSp = workspace.Parent.lock();
        Lighting* workspaceLighting = findLightingInTree(static_cast<Instance*>(&workspace));
        Lighting* systemLighting = findLightingInTree(systemSp ? systemSp.get() : static_cast<Instance*>(&workspace));
        if (lightingHasSkyboxSource(workspaceLighting)) {
            lighting = workspaceLighting;
        } else if (lightingHasSkyboxSource(systemLighting)) {
            lighting = systemLighting;
        } else {
            lighting = workspaceLighting ? workspaceLighting : systemLighting;
        }
    }
    if (lighting) {
        bool hasAnyPath = false;
        for (int i = 0; i < 6; i++) if (!lighting->skyboxPaths[i].empty()) { hasAnyPath = true; break; }
        if (!hasAnyPath && lighting->cubemapTexture != 0) {
            glDeleteTextures(1, &lighting->cubemapTexture);
            lighting->cubemapTexture = 0;
        }
        if (hasAnyPath && (lighting->skyboxDirty || lighting->cubemapTexture == 0)) {
            if (lighting->cubemapTexture != 0) {
                glDeleteTextures(1, &lighting->cubemapTexture);
                lighting->cubemapTexture = 0;
            }
            std::string skyboxPaths[6];
            std::string fallbackPath;
            for (int i = 0; i < 6; i++) {
                if (!lighting->skyboxPaths[i].empty()) {
                    fallbackPath = lighting->skyboxPaths[i];
                    break;
                }
            }
            for (int i = 0; i < 6; i++) {
                skyboxPaths[i] = lighting->skyboxPaths[i].empty() ? fallbackPath : lighting->skyboxPaths[i];
            }
            lighting->cubemapTexture = loadCubemap(skyboxPaths);
        }
        lighting->skyboxDirty = false;
    }

    // ---- Shadow Pass ----
    Matrix4 lightSpaceMatrix;
    bool shadowReady = false;
    if (lighting && shadowFBO && shadowMapTex && depthShader) {
        // lightDir を正規化して light eye 位置を計算
        Vector3 ld = lighting->lightDir;
        float len = std::sqrt(ld.x*ld.x + ld.y*ld.y + ld.z*ld.z);
        if (len > 0.001f) { ld.x /= len; ld.y /= len; ld.z /= len; }
        Vector3 lightEye(-ld.x * 80.0f, -ld.y * 80.0f, -ld.z * 80.0f);
        Vector3 upVec = (std::fabsf(ld.y) < 0.99f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
        Matrix4 lightView = Matrix4::LookAt(lightEye, Vector3(0.0f, 0.0f, 0.0f), upVec);
        Matrix4 lightProj = Matrix4::Ortho(-80.0f, 80.0f, -80.0f, 80.0f, 0.1f, 400.0f);
        lightSpaceMatrix = lightProj * lightView;

        GLint prevFBO = 0;
        GLint prevViewport[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);

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
                if (bc->Color.a > 0.001f) {
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
        for (auto const& [name, child] : workspace.getChildren()) {
            shadowRender(shadowRender, child.get());
        }

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        shadowReady = true;
    }

    // ---- Skybox ----
    if (lighting && lighting->cubemapTexture != 0 && skyboxVAO && skyboxShader) {
        GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
        GLboolean prevDepthMask = GL_TRUE;
        GLint prevDepthFunc = GL_LESS;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_ALWAYS);
        glUseProgram(skyboxShader);
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "view"),       1, GL_FALSE, view.m);
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "projection"), 1, GL_FALSE, projection.m);
        glBindVertexArray(skyboxVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, lighting->cubemapTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        glDepthMask(prevDepthMask);
        glDepthFunc(prevDepthFunc);
        if (prevCull) glEnable(GL_CULL_FACE);
    }

    // ---- Main Pass ----
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"),       1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection.m);
    glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"),
                user.cpos.x, user.cpos.y, user.cpos.z);
    if (lighting) {
        if (lightDirLoc   != -1) glUniform3f(lightDirLoc,   lighting->lightDir.x,  lighting->lightDir.y,  lighting->lightDir.z);
        if (brightnessLoc != -1) glUniform1f(brightnessLoc, lighting->brightness);
    }
    // Shadow map をテクスチャユニット 1 にバインド
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowMapTex ? shadowMapTex : 0);
    glActiveTexture(GL_TEXTURE0);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "lightSpaceMatrix"), 1, GL_FALSE, lightSpaceMatrix.m);
    glUniform1f(glGetUniformLocation(shaderProgram, "hasShadows"),
                shadowReady ? 1.0f : 0.0f);

    int modelLoc = glGetUniformLocation(shaderProgram, "model");
    glBindVertexArray(VAO);

    auto renderInstances = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        if (inst->IsA("Cube")) {
            Cube* cube = static_cast<Cube*>(inst);
            if (cube->Color.a > 0.001f) {
                Matrix4 modelMat = cube->getWorldCFrame().toMatrix4() *
                                   Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
                cube->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("Cylinder")) {
            Cylinder* c = static_cast<Cylinder*>(inst);
            if (c->Color.a > 0.001f) {
                Matrix4 modelMat = c->getWorldCFrame().toMatrix4() *
                                   Matrix4::Scale(c->Size.x, c->Size.y, c->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
                c->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("TriangularPrism")) {
            TriangularPrism* tp = static_cast<TriangularPrism*>(inst);
            if (tp->Color.a > 0.001f) {
                Matrix4 modelMat = tp->getWorldCFrame().toMatrix4() *
                                   Matrix4::Scale(tp->Size.x, tp->Size.y, tp->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
                tp->draw(modelLoc, shaderProgram);
            }
        } else if (inst->IsA("Sphere")) {
            Sphere* sp = static_cast<Sphere*>(inst);
            if (sp->Color.a > 0.001f) {
                Matrix4 modelMat = sp->getWorldCFrame().toMatrix4() *
                                   Matrix4::Scale(sp->Size.x, sp->Size.y, sp->Size.z);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
                sp->draw(modelLoc, shaderProgram);
            }
        }
        for (auto const& [name, child] : inst->getChildren()) {
            self(self, child.get());
        }
    };

    for (auto const& [name, child] : workspace.getChildren()) {
        renderInstances(renderInstances, child.get());
    }

    // ---- 選択インスタンスの黄色ワイヤーフレームハイライト ----
    if (editor && editor->hierarchyPanel->selectedInstance) {
        Instance* sel = editor->hierarchyPanel->selectedInstance;
        if (sel->IsA("BaseCube")) {
            BaseCube* bc = static_cast<BaseCube*>(sel);
            Matrix4 modelMat = bc->getWorldCFrame().toMatrix4() *
                               Matrix4::Scale(bc->Size.x * 1.02f,
                                              bc->Size.y * 1.02f,
                                              bc->Size.z * 1.02f);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
            int colorLocHl = glGetUniformLocation(shaderProgram, "ourColor");
            if (colorLocHl != -1) glUniform4f(colorLocHl, 1.0f, 1.0f, 0.0f, 1.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTexture);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(2.0f);
            // 形状ごとに正しいVAOをバインドしてワイヤーフレームを描画
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

// ===================================================
//  メインループから呼ぶ統合描画
// ===================================================
void Renderer::render(User& user, GLFWwindow* window, Workspace& workspace) {
    // 1. FBO に 3D シーンを描画
    if (editor) {
        editor->beginViewportRender();
        renderScene(user, workspace);
        editor->endViewportRender();
    }

    // 2. 既定のフレームバッファをクリア（ImGui 用）
    int winW, winH;
    glfwGetFramebufferSize(window, &winW, &winH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 3. ImGui（DockSpace + パネル）
    renderImGui(user, window, workspace);

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

    unsigned int textureID = 0;
    if (!data) {
        std::cout << "Failed to load texture: " << path << std::endl;
        return 0;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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

// ===================================================
//  キューブマップ読み込み（Skybox 用）
// ===================================================
unsigned int Renderer::loadCubemap(const std::string paths[6]) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    stbi_set_flip_vertically_on_load(false); // キューブマップは反転不要

    for (int i = 0; i < 6; i++) {
        if (paths[i].empty()) {
            unsigned char white[] = { 200, 200, 200, 255 };
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        } else {
            int w, h, ch;
            unsigned char* data = stbi_load(paths[i].c_str(), &w, &h, &ch, 4);
            if (data) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            } else {
                unsigned char white[] = { 200, 200, 200, 255 };
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
                std::cout << "[Renderer] Cubemap face " << i << " load failed: " << paths[i] << std::endl;
            }
        }
    }

    stbi_set_flip_vertically_on_load(true); // 他のテクスチャ用に戻す

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}
