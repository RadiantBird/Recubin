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
    return {
        h, h, h,  h,-h, h, -h,-h, h, -h, h, h, // 前面
        h, h,-h,  h,-h,-h, -h,-h,-h, -h, h,-h  // 背面
    };
}

class Cube {
    public:
        Vector3 Position = Vector3(0.0, 0.0, 0.0);
        Vector3 Size = Vector3(4.0, 1.0, 2.0);

        static constexpr unsigned int indices[] = {
            0, 1, 3,  1, 2, 3, // 前面
            4, 5, 7,  5, 6, 7, // 背面
            0, 1, 4,  1, 5, 4, // 右面
            2, 3, 6,  3, 7, 6, // 左面
            0, 3, 4,  3, 7, 4, // 上面
            1, 2, 5,  2, 6, 5  // 下面
        };

        void draw(int modelLoc) {
            // 1. 移動行列を作る
            Matrix4 translation = Matrix4::Translate(Position.x, Position.y, Position.z);
            
            // 2. 拡大縮小行列を作る（Sizeベクトルをそのまま使う）
            Matrix4 scaling = Matrix4::Scale(Size.x, Size.y, Size.z);
            
            // 3. 合成する（Scaleを先に掛けるのがルール！）
            Matrix4 model = translation * scaling;
            
            // 4. GPUに「加工済みの伝票」を送る
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
            
            // 5. 描画！
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        Cube(Vector3 Pos, Vector3 Sz) : Position(Pos), Size(Sz) {}
};

constexpr unsigned int Cube::indices[];

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

    // glBufferData(GL_ARRAY_BUFFER, 
    //              vertices.size() * sizeof(float), // 正確なバイト数
    //              vertices.data(),                 // これが const void* (内部配列へのポインタ) になる
    //              GL_STATIC_DRAW);
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW); 

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
                sizeof(Cube::indices), 
                Cube::indices, 
                GL_STATIC_DRAW);

    // 属性の設定（ここも忘れずに）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // read position 0(location), for each 3 floats, skip 3 * sizeof float.
    
    glEnableVertexAttribArray(0); 

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // classic CPU style

    int rotateX = 0, rotateY = 0, rotateZ = 0;
    Vector3 cam(0, 0, 5);

    // 描画したい Cube たちを並べる

    std::vector<Cube> world = {
        Cube({0, 0, -2}, {1, 4, 1}),
        Cube({2, 0, -4}, {1, 1, 1}),
        Cube({-2, 0, -4}, {2, 1, 1})
    };

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
            c.draw(modelLoc);
        }
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}