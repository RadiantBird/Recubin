#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

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
    return {
        h, h, h,  h,-h, h, -h,-h, h, -h, h, h, // 前面
        h, h,-h,  h,-h,-h, -h,-h,-h, -h, h,-h  // 背面
    };
}

class Vector3 {
    public:
        float x, y, z;

        Vector3 operator+(const Vector3& v) const { return {x + v.x, y + v.y, z + v.z}; }
        Vector3 operator-(const Vector3& v) const { return {x - v.x, y - v.y, z - v.z}; }
        Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }

        // ベクトルの長さ（距離）を計算
        float length() const { return std::sqrt(x * x + y * y + z * z); }

        // 正規化（長さを1にする：方向だけが欲しいとき）
        Vector3 normalize() const {
            float l = length();
            return (l > 0) ? (*this * (1.0f / l)) : Vector3{0, 0, 0};
        }

        static Vector3 Cross(const Vector3& a, const Vector3& b) {
            return Vector3(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            );
        }
        
        Vector3(float x, float y, float z) {
            this->x = x;
            this->y = y;
            this->z = z;
        }
};

struct Matrix4 {
    float m[16];

    // 1. デフォルト：単位行列 (Identity)
    Matrix4() {
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // 2. 行列の掛け算 (A * B) - これがすべての基本
    Matrix4 operator*(const Matrix4& b) const {
        Matrix4 res;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                res.m[col * 4 + row] =
                    m[0 * 4 + row] * b.m[col * 4 + 0] +
                    m[1 * 4 + row] * b.m[col * 4 + 1] +
                    m[2 * 4 + row] * b.m[col * 4 + 2] +
                    m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return res;
    }

    // 3. 移動 (Translation)
    static Matrix4 Translate(float x, float y, float z) {
        Matrix4 res; // 単位行列で初期化される
        res.m[12] = x;
        res.m[13] = y;
        res.m[14] = z;
        return res;
    }

    // 4. 拡大縮小 (Scale)
    static Matrix4 Scale(float x, float y, float z) {
        Matrix4 res;
        res.m[0] = x;
        res.m[5] = y;
        res.m[10] = z;
        return res;
    }

    // 5. 回転 (Rotation) - 各軸
    static Matrix4 RotateX(float degree) {
        Matrix4 res;
        float r = degree * 3.14159265f / 180.0f;
        res.m[5] = cos(r);  res.m[6] = sin(r);
        res.m[9] = -sin(r); res.m[10] = cos(r);
        return res;
    }

    static Matrix4 RotateY(float degree) {
        Matrix4 res;
        float r = degree * 3.14159265f / 180.0f;
        res.m[0] = cos(r);  res.m[2] = -sin(r);
        res.m[8] = sin(r);  res.m[10] = cos(r);
        return res;
    }

    static Matrix4 RotateZ(float degree) {
        Matrix4 res;
        float r = degree * 3.14159265f / 180.0f;
        res.m[0] = cos(r);  res.m[1] = sin(r);
        res.m[4] = -sin(r); res.m[5] = cos(r);
        return res;
    }

    // 6. 投影 (Perspective) - 遠近感
    static Matrix4 Perspective(float fovDeg, float aspect, float zNear, float zFar) {
        Matrix4 res;
        for(int i=0; i<16; i++) res.m[i] = 0.0f; // 一旦クリア
        float f = 1.0f / tan(fovDeg * 3.14159265f / 360.0f);
        res.m[0] = f / aspect;
        res.m[5] = f;
        res.m[10] = (zFar + zNear) / (zNear - zFar);
        res.m[11] = -1.0f;
        res.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return res;
    }
};

int main() {
    std::cout << "Hello world!\n";
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

    std::vector<float> vertices = createCubeVertices(0.5f);

    unsigned int indices[] = {
        0, 1, 3,  1, 2, 3, // 前面
        4, 5, 7,  5, 6, 7, // 背面
        0, 1, 4,  1, 5, 4, // 右面
        2, 3, 6,  3, 7, 6, // 左面
        0, 3, 4,  3, 7, 4, // 上面
        1, 2, 5,  2, 6, 5  // 下面
    }; 

    unsigned int VBO;
    unsigned int VAO;
    unsigned int EBO;

    glGenBuffers(1, &EBO);
    glGenVertexArrays(1, &VAO);  
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    glBufferData(GL_ARRAY_BUFFER, 
                 vertices.size() * sizeof(float), // 正確なバイト数
                 vertices.data(),                 // これが const void* (内部配列へのポインタ) になる
                 GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW); 

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

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    // read position 0(location), for each 3 floats, skip 3 * sizeof float.
    
    glEnableVertexAttribArray(0); 

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // classic CPU style

    int rotateX = 0, rotateY = 0, rotateZ = 0;
    Vector3 cam(0, 0, -5);
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        float speed = 0.05f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            cam.z += speed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            cam.z -= speed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            cam.x += speed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            cam.x -= speed;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            cam.y += speed;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            cam.y -= speed;
        }

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            rotateY = (rotateY + 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            rotateY = (rotateY - 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            rotateX = (rotateX + 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            rotateX = (rotateX - 1) % 360;
        }

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        // --- 行列の組み立て (Matrix4 クラスを使用) ---

        // 1. 投影行列 (遠近感)
        Matrix4 projection = Matrix4::Perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);

        // 2. ビュー行列 (カメラ)
        // 回転してから移動させることで、カメラ中心の挙動にする
        Matrix4 view = Matrix4::RotateX((float)rotateX) * Matrix4::RotateY((float)rotateY) * Matrix4::Translate(cam.x, cam.y, cam.z);

        // 3. モデル行列 (物体の配置)
        // 立方体を少し奥(-2.0f)に置いて、その場で回転させる例
        Matrix4 model = Matrix4::Translate(0.0f, 0.0f, -2.0f);

        // --- GPUへのUniform転送 ---
        int modelLoc = glGetUniformLocation(shaderProgram, "model");
        int viewLoc = glGetUniformLocation(shaderProgram, "view");
        int projeLoc = glGetUniformLocation(shaderProgram, "projection");

        // クラス内の配列 m を直接渡す。列優先なので GL_FALSE
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(projeLoc, 1, GL_FALSE, projection.m);

        // --- 描画実行 ---
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}