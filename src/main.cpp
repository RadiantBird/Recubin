#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <include/Math/Matrix4.hpp>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstddef>

#define STB_IMAGE_IMPLEMENTATION
#include "include/stb_image.h"

const float pi = 3.14159265f;

std::string loadShaderSource(const char* filePath) {
    std::string content;
    std::ifstream fileStream(filePath, std::ios::in);

    if (!fileStream.is_open()) {
        std::cerr << "Could not read file " << filePath << ". File does not exist." << std::endl;
        return "";
    }

    std::stringstream sstr;
    sstr << fileStream.rdbuf();
    content = sstr.str();
    fileStream.close();
    
    return content;
}

struct Vertex {
    Vector3 Position;  // 3 floats
    Vector3 Normal;    // 3 floats
    float U, V;        // 2 floats

    Vertex() : Position(0, 0, 0), Normal(0, 0, 0), U(0), V(0) {}
};

std::vector<Vertex> createCubeVertices(float size) {
    float h = size / 2.0f;
    std::vector<Vertex> v;

    // 6つの面の向き（法線）を定義
    struct Face { float nx, ny, nz; };
    Face faces[6] = {
        { 0, 0, 1}, { 0, 0,-1}, // 前, 後
        { 0, 1, 0}, { 0,-1, 0}, // 上, 下
        { 1, 0, 0}, {-1, 0, 0}  // 右, 左
    };

    for (int i = 0; i < 6; i++) {
        float nx = faces[i].nx, ny = faces[i].ny, nz = faces[i].nz;
        
        // 法線に垂直な2つのベクトル（面を作るための横と縦の棒）を計算
        float ux = (nx == 0) ? 1 : 0;
        float uy = (nx != 0 || nz != 0) ? 0 : 1;
        float uz = (nz == 0 && nx != 0) ? 1 : 0;
        if (ny != 0) { ux = 1; uy = 0; uz = 0; } // 上下だけ特殊処理
        
        float vx = ny * uz - nz * uy;
        float vy = nz * ux - nx * uz;
        float vz = nx * uy - ny * ux;

        // 4つの角を計算して vector にブチ込む
        // UV座標の対応: 右上(1,1), 右下(1,0), 左下(0,0), 左上(0,1) 
        float p[4][2] = {{1,1}, {1,-1}, {-1,-1}, {-1,1}};
        float uv[4][2] = {{1,1}, {1,0}, {0,0}, {0,1}}; // UVの並び

        for (int j = 0; j < 4; j++) {
            Vertex vert;

            // 1. 座標 (x, y, z) を Vertex 構造体のメンバに代入
            vert.Position.x = nx * h + p[j][0] * ux * h + p[j][1] * vx * h;
            vert.Position.y = ny * h + p[j][0] * uy * h + p[j][1] * vy * h;
            vert.Position.z = nz * h + p[j][0] * uz * h + p[j][1] * vz * h;

            // 2. 法線 (nx, ny, nz) を代入
            vert.Normal.x = nx;
            vert.Normal.y = ny;
            vert.Normal.z = nz;

            // 3. UV座標 (u, v) を代入
            vert.U = uv[j][0];
            vert.V = uv[j][1];

            // 構造体ごと vector に追加
            v.push_back(vert);
        }
    }
    return v;
}

struct Color4 {
    float r, g, b, a;

    Color4(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}

    // 0-255
    static Color4 FromRGB(int r, int g, int b, float a = 1.0f) {
        return Color4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    }
};

class Cube {
    public:
        Vector3 Position = Vector3(0.0, 0.0, 0.0);
        Vector3 Size = Vector3(4.0, 1.0, 2.0);
        Color4 Color = Color4(1, 1, 1, 1);
        unsigned int faceTextures[6] = {0, 0, 0, 0, 0, 0};

        void setFaceTexture(int faceIdx, unsigned int texID) {
            if (faceIdx >= 0 && faceIdx < 6) faceTextures[faceIdx] = texID;
        }

        void draw(int modelLoc, int shaderProgram) {
            // 行列の転送
            Matrix4 translation = Matrix4::Translate(Position.x, Position.y, Position.z);
            Matrix4 scaling = Matrix4::Scale(Size.x, Size.y, Size.z);
            Matrix4 model = translation * scaling; 
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);

            // 【渡し忘れチェック】ourColor を確実に転送
            int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
            if (colorLoc != -1) {
                glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
            }

            for (int i = 0; i < 6; i++) {
                glActiveTexture(GL_TEXTURE0); 
                // ここで 0 をチェックする必要はありません。設定がなければ自動的に whiteTexture が入っています。
                glBindTexture(GL_TEXTURE_2D, faceTextures[i]);

                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(uintptr_t)(i * 6 * sizeof(unsigned int)));
            }
        }

        Cube(Vector3 Pos, Vector3 Sz, unsigned int defaultTex) : Position(Pos), Size(Sz) {
            for(int i = 0; i < 6; i++) faceTextures[i] = defaultTex;
        }
};

struct camera {
    int rotateX = 0, rotateY = 0, rotateZ = 0;
    Vector3 Position = Vector3(0, 0, 5);
};

class User {
    public:
        GLFWwindow* window = nullptr;

        float speed = 0.05f;
        camera current_camera;
        camera &cam = current_camera;
        Vector3 &cpos = cam.Position;

        float radX = cam.rotateX * pi / 180.0f;
        float radY = cam.rotateY * pi / 180.0f;

        Vector3 forward;
        Vector3 right;
        Vector3 up;

        bool wannaExit = false;

        // ベクトルを最新の回転角から計算し直す関数
        void updateVectors() {
            float radX = cam.rotateX * pi / 180.0f;
            float radY = cam.rotateY * pi / 180.0f;

            forward = Vector3(
                sin(radY) * cos(radX),
                -sin(radX),
                -cos(radY) * cos(radX)
            ).normalize();

            right = Vector3::Cross(forward, Vector3(0, 1, 0)).normalize();
            up = Vector3::Cross(right, forward).normalize();
        }

        void processInput() {
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
                cpos = cpos + forward * speed;
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                cpos = cpos - forward * speed;
            }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                cpos = cpos - right * speed;
            }
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                cpos = cpos + right * speed;
            }
            // 上下移動は世界軸のYで動くのが直感的
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
                cpos.y += speed;
            }
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
                cpos.y -= speed;
            }

            if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) cam.rotateY = (cam.rotateY + 1) % 360;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) cam.rotateY = (cam.rotateY - 1) % 360;
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) cam.rotateX = (cam.rotateX + 1) % 360;
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) cam.rotateX = (cam.rotateX - 1) % 360;

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                wannaExit = true;
            }
        }

        User(GLFWwindow* window) : forward(0, 0, -1), right(1, 0, 0), up(0, 1, 0) {
            this->window = window;
            updateVectors();
        }
};

class Renderer {
    public:
        unsigned int VBO;
        unsigned int VAO;
        unsigned int EBO;

        unsigned int shaderProgram;

        std::vector<unsigned int> indices = {};

        unsigned int whiteTexture;
        void createWhiteTexture() {
            glGenTextures(1, &whiteTexture);
            glBindTexture(GL_TEXTURE_2D, whiteTexture);
            
            // 1x1の白いピクセルデータ（RGBA）
            unsigned char white[] = { 255, 255, 255, 255 }; 
            
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }

        void init() {
            int maxSize;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
            std::cout << "Max Texture Size: " << maxSize << std::endl;

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

            std::string vShaderStr = loadShaderSource("src/vertex.glsl");
            std::string fShaderStr = loadShaderSource("src/fragment.glsl");

            const char *vertexShaderSource = vShaderStr.c_str();
            const char *fragmentShaderSource = fShaderStr.c_str();

            unsigned int vertexShader;
            vertexShader = glCreateShader(GL_VERTEX_SHADER);

            glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
            glCompileShader(vertexShader);

            int  success; // reusing this variable
            char infoLog[512];
            glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);

            if(!success) {
                glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
                std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
            }

            unsigned int fragmentShader;
            fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
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
            if(!success) {
                glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog3);
                std::cout << "ERROR::SHADER::PROGRAM::LINK_FAILED\n" << infoLog3 << std::endl;
            }

            glUseProgram(shaderProgram);
            glEnable(GL_DEPTH_TEST);
            // ブレンド機能を有効にする
            glEnable(GL_BLEND);
            // 計算式の設定：(ソースのアルファ * ソースの色) + ((1 - ソースのアルファ) * 背景の色)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);  

            Cube testCube({0, 0, 0}, {1, 1, 1}, whiteTexture);

            glBindVertexArray(VAO);

            std::vector<Vertex> standardVertices = createCubeVertices(1.0f);
                
            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferData(GL_ARRAY_BUFFER, 
                        standardVertices.size() * sizeof(Vertex), 
                        standardVertices.data(), 
                        GL_STATIC_DRAW);

            // インデックスをEBOに転送 (Cube::indices は static なのでどこからでも取れる)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, 
                        indices.size() * sizeof(unsigned int), 
                        indices.data(), 
                        GL_STATIC_DRAW);


            GLsizei stride = sizeof(Vertex);
            // 0: Position
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Position));
            glEnableVertexAttribArray(0);

            // 1: Normal
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Normal));
            glEnableVertexAttribArray(1);

            // 2: TexCoord (ここがズレると「色がくそ」になる)
            // 構造体の中にある U の場所を直接指定
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, U));
            glEnableVertexAttribArray(2);

            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            int lightPosLoc = glGetUniformLocation(shaderProgram, "lightPos");
            int lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");

            // 太陽光のような白い光を、右斜め上から
            glUniform3f(lightPosLoc, 10.0f, 10.0f, 10.0f); 
            glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);

            createWhiteTexture(); // failsafe用
        }

        void render(User &user, GLFWwindow* window, std::vector<Cube> &world) {
            Matrix4 projection = Matrix4::Perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);

            // 今の camera 位置から、forward 方向にある「注視点」を割り出す
            Vector3 target = user.cpos + user.forward; 
            
            // cam(位置), target(見たい場所), up(どっちが上か) を渡すだけ
            Matrix4 view = Matrix4::LookAt(user.cpos, target, Vector3(0, 1, 0));

            Matrix4 model = Matrix4::Translate(0.0f, 0.0f, -2.0f);

            glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 10.0f, 10.0f, 10.0f);
            glUniform3f(glGetUniformLocation(shaderProgram, "lightColor"), 1.0f, 1.0f, 1.0f);
            glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), user.cpos.x, user.cpos.y, user.cpos.z);

            // 3. テクスチャユニット0番を使うことを明示
            glUniform1i(glGetUniformLocation(shaderProgram, "ourTexture"), 0);

            int modelLoc = glGetUniformLocation(shaderProgram, "model");
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, view.m);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection.m);

            glBindVertexArray(VAO);
            for (Cube& c : world) {
                c.draw(modelLoc, shaderProgram);
            }
            
            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        unsigned int loadTexture(const char* path) {
            int width, height, nrChannels;
            // ここで flip を呼ぶのをやめる（main の冒頭で1回だけ呼ぶ）

            unsigned char *data = stbi_load(path, &width, &height, &nrChannels, 4); // RGBAに固定

            unsigned int textureID = 0; // 初期化
            if (!data) {
                std::cout << "Failed: " << path << std::endl;
                return 0;
            }

            // ロードされた実際のチャンネル数は無視して、OpenGLには「4chだぞ」と教える
            glGenTextures(1, &textureID);
            glBindTexture(GL_TEXTURE_2D, textureID);

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 

            // 全部 RGBA で統一して送る
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            // 基本的なパラメータを設定（これをしないと不安定になる環境がある）
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            std::cout << "Success: " << path << " (" << nrChannels << "ch)" << std::endl;
            
            stbi_image_free(data);
            return textureID;
        }
};

GLFWwindow* setupWindow() {
    std::cout << "initing GLFW...\n";
    if (!glfwInit()) {
        std::cout << "GLFW init failed\n";
        return nullptr;
    }

    std::cout << "creating window...\n";
    GLFWwindow* window = glfwCreateWindow(800, 600, "Welcome to Recubin", nullptr, nullptr);
    if (!window) {
        std::cout << "Window creation failed\n";
        glfwTerminate();
        return nullptr;
    }

    std::cout << "making context...\n";
    glfwMakeContextCurrent(window);

    std::cout << "initing GLEW...\n";
    if (glewInit() != GLEW_OK) {
        std::cout << "GLEW init failed\n";
        return nullptr;
    }
    return window;
}

int main() {
    std::cout << "Hello world!!\n";
    
    GLFWwindow* window = setupWindow();
    if (!window) {
        std::cout << "[ERROR] Failed to setup.\n";
        return -1;
    }

    User user(window);
    Renderer renderer;
    renderer.init();

    stbi_set_flip_vertically_on_load(true); // OpenGL用
    unsigned int floppa   = renderer.loadTexture("assets/image/floppa2048.jpg");
    unsigned int thecat   = renderer.loadTexture("assets/image/the-cat.png");
    unsigned int saladcat = renderer.loadTexture("assets/image/salad-cat.jpg");
    unsigned int smile    = renderer.loadTexture("assets/image/smile.png");
    unsigned int bliss    = renderer.loadTexture("assets/image/bliss.jpg");
    unsigned int limabis  = renderer.loadTexture("assets/image/Limabis_logo.png");

    // make it workspace we ain't unity
    std::vector<Cube> world = {
        Cube({0, 0, -2}, {1, 4, 1}, renderer.whiteTexture),
        Cube({2, 0, -4}, {1, 1, 1}, renderer.whiteTexture),
        Cube({-2, 0, -4}, {2, 1, 1}, renderer.whiteTexture),
        Cube({0, 0, -8}, {2, 2, 2}, renderer.whiteTexture)
    };

    world[0].Color = Color4(0, 0, 1, 1);
    world[1].Color = Color4(1, 0, 0, 1);
    world[2].Color = Color4(0, 1, 0, 1);
    Cube &C = world[3];
    C.Color = Color4(1, 1, 0, 1);
    C.setFaceTexture(0, floppa);
    C.setFaceTexture(1, thecat);
    C.setFaceTexture(2, saladcat);
    C.setFaceTexture(3, smile);
    C.setFaceTexture(4, bliss);
    C.setFaceTexture(5, limabis);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);

        // --- 1. 現在の向き(Forward)を計算 ---
        user.updateVectors();

        // --- 2. 入力検知 (向きに基づいた移動) ---
        user.processInput();
        if (user.wannaExit) {
            break;
        }

        renderer.render(user, window, world);
    }

    glfwTerminate();

    return 0;
}