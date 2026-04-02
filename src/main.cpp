#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

    int x_diff = 0, y_diff = 0;
    int rotateX = 0, rotateY = 0, rotateZ = 0;

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            y_diff += 1;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            y_diff -= 1;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            x_diff -= 1;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            x_diff += 1;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            rotateY = (rotateY + 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            rotateY = (rotateY - 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }
        
        float rx = rotateX * pi / 180.0f;
        float ry = rotateY * pi / 180.0f;
        float rz = rotateZ * pi / 180.0f;

        float cx = cos(rx), sx = sin(rx);
        float cy = cos(ry), sy = sin(ry);
        float cz = cos(rz), sz = sin(rz);

        // 3つの回転行列を掛け合わせた結果（列優先）
        float transform[] = {
            // 第1列
            cy * cz,                          cy * sz,                          -sy,      0.0f,
            // 第2列
            sx * sy * cz - cx * sz,           sx * sy * sz + cx * cz,           sx * cy,  0.0f,
            // 第3列
            cx * sy * cz + sx * sz,           cx * sy * sz - sx * cz,           cx * cy,  0.0f,
            // 第4列（x, y 移動 + zはカメラから離すために -2.0f 固定か変数で）
            x_diff / 100.0f,                  y_diff / 100.0f,                  -2.0f,    1.0f 
        };

        float aspect = 800.0f / 600.0f; // ウィンドウの縦横比
        float fov = 45.0f * pi / 180.0f; // 視野角
        float f = 1.0f / tan(fov / 2.0f);
        float zNear = 0.1f;
        float zFar = 100.0f;

        float projection[16] = {
            f / aspect, 0.0f, 0.0f, 0.0f,
            0.0f, f, 0.0f, 0.0f,
            0.0f, 0.0f, (zFar + zNear) / (zNear - zFar), -1.0f, // ここでZ値をWに放り込むのがコツ
            0.0f, 0.0f, (2.0f * zFar * zNear) / (zNear - zFar), 0.0f
        };

        int modelLoc = glGetUniformLocation(shaderProgram, "model");
        int projeLoc = glGetUniformLocation(shaderProgram, "projection");
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, transform); // 列優先なのでFALSE
        glUniformMatrix4fv(projeLoc, 1, GL_FALSE, projection);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}