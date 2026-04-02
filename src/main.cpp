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

std::vector<float> createCubeVertices(float size) {
    float h = size / 2.0f;
    std::vector<float> v;

    // 6つの面の向き（法線）を定義
    struct Face { float nx, ny, nz; };
    Face faces[6] = {
        { 0, 0, 1}, { 0, 0,-1}, // 前, 後
        { 0, 1, 0}, { 0,-1, 0}, // 上, 下
        { 1, 0, 0}, {-1, 0, 0}  // 右, 左
    };

    for (int i = 0; i < 6; i++) {
        float nx = faces[i].nx, ny = faces[i].ny, nz = faces[i].nz;
        
        // 法線に垂直な2つのベクトル（面を作るための横と縦の棒）を適当に決める
        // 本来は外積を使いますが、立方体ならこれで十分
        float ux = (nx == 0) ? 1 : 0;
        float uy = (nx != 0 || nz != 0) ? 0 : 1;
        float uz = (nz == 0 && nx != 0) ? 1 : 0;
        if (ny != 0) { ux = 1; uy = 0; uz = 0; } // 上下だけ特殊処理
        
        float vx = ny * uz - nz * uy;
        float vy = nz * ux - nx * uz;
        float vz = nx * uy - ny * ux;

        // 4つの角を計算して vector にブチ込む
        float p[4][2] = {{1,1}, {1,-1}, {-1,-1}, {-1,1}};
        for (int j = 0; j < 4; j++) {
            // 座標 (x, y, z)
            v.push_back(nx * h + p[j][0] * ux * h + p[j][1] * vx * h);
            v.push_back(ny * h + p[j][0] * uy * h + p[j][1] * vy * h);
            v.push_back(nz * h + p[j][0] * uz * h + p[j][1] * vz * h);
            // 法線 (nx, ny, nz)
            v.push_back(nx); v.push_back(ny); v.push_back(nz);
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
        Color4 Color = Color4(0, 1, 0, 1);

        void draw(int modelLoc, int shaderProgram) {
            Matrix4 translation = Matrix4::Translate(Position.x, Position.y, Position.z);
            
            Matrix4 scaling = Matrix4::Scale(Size.x, Size.y, Size.z);
            
            // 合成する（Scaleを先に掛ける）
            Matrix4 model = translation * scaling;
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
            
            int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
            glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        Cube(Vector3 Pos, Vector3 Sz) : Position(Pos), Size(Sz) {}
};

int main() {
    std::cout << "Hello world!!\n";
    if (!glfwInit()) {
        std::cout << "GLFW init failed\n";
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(800, 600, "Welcome to Recubin", nullptr, nullptr);
    if (!window) {
        std::cout << "Window creation failed\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK) {
        std::cout << "GLEW init failed\n";
        return -1;
    }

    unsigned int VBO;
    unsigned int VAO;
    unsigned int EBO;

    glGenBuffers(1, &EBO);
    glGenVertexArrays(1, &VAO);  
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    std::vector<unsigned int> indices = {};
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
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    unsigned int shaderProgram;
    shaderProgram = glCreateProgram();

    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    char infoLog3[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if(!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINK_FAILED\n" << infoLog << std::endl;
    }

    glUseProgram(shaderProgram);
    glEnable(GL_DEPTH_TEST);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);  

    Cube testCube({0, 0, 0}, {1, 1, 1});

    glBindVertexArray(VAO);

    std::vector<float> standardVertices = createCubeVertices(1.0f);
    // 頂点データをVBOに転送
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, 
                standardVertices.size() * sizeof(float), 
                standardVertices.data(), 
                GL_STATIC_DRAW);

    // インデックスをEBOに転送 (Cube::indices は static なのでどこからでも取れる)
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 
                indices.size() * sizeof(unsigned int), 
                indices.data(), 
                GL_STATIC_DRAW);


    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // read position 0(location), for each 3 floats, skip 3 * sizeof float.
    
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // classic CPU style

    int rotateX = 0, rotateY = 0, rotateZ = 0;
    Vector3 cam(0, 0, 5);

    // 描画したい Cube たちを並べる

    std::vector<Cube> world = {
        Cube({0, 0, -2}, {1, 4, 1}),
        Cube({2, 0, -4}, {1, 1, 1}),
        Cube({-2, 0, -4}, {2, 1, 1})
    };

    world[0].Color = Color4(0, 0, 1, 1);
    world[1].Color = Color4(1, 0, 0, 1);

    int lightPosLoc = glGetUniformLocation(shaderProgram, "lightPos");
    int lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");

    // 太陽光のような白い光を、右斜め上から
    glUniform3f(lightPosLoc, 10.0f, 10.0f, 10.0f); 
    glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        // --- 1. 現在の向き(Forward)を計算 ---
        float radX = rotateX * pi / 180.0f;
        float radY = rotateY * pi / 180.0f;

        Vector3 forward(
            sin(radY) * cos(radX),
            -sin(radX),
            -cos(radY) * cos(radX)
        );
        forward = forward.normalize();

        // 右方向は (前 x 世界の上)
        Vector3 right = Vector3::Cross(forward, Vector3(0, 1, 0)).normalize();
        // 上方向は (右 x 前)
        Vector3 up = Vector3::Cross(right, forward).normalize();

        // --- 2. 入力検知 (向きに基づいた移動) ---
        float speed = 0.05f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            cam = cam + forward * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            cam = cam - forward * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            cam = cam - right * speed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            cam = cam + right * speed;
        }
        // 上下移動は世界軸のYで動くのが直感的（クリエイティブモード風）
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            cam.y += speed;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            cam.y -= speed;
        }

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) rotateY = (rotateY + 1) % 360;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) rotateY = (rotateY - 1) % 360;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) rotateX = (rotateX + 1) % 360;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) rotateX = (rotateX - 1) % 360;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        // --- 3. 行列の組み立て ---
        Matrix4 projection = Matrix4::Perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);

        // 今の camera 位置から、forward 方向にある「注視点」を割り出す
        Vector3 target = cam + forward; 
        
        // cam(位置), target(見たい場所), up(どっちが上か) を渡すだけ
        Matrix4 view = Matrix4::LookAt(cam, target, Vector3(0, 1, 0));

        Matrix4 model = Matrix4::Translate(0.0f, 0.0f, -2.0f);

        // --- 4. Uniform転送と描画 ---
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

    glfwTerminate();

    return 0;
}