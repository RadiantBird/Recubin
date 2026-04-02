#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

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

    float vertices[] = {
        0.25f,  0.25f, 0.0f,  // top right
        0.25f, -0.25f, 0.0f,  // bottom right
        -0.25f, -0.25f, 0.0f,  // bottom left
        -0.25f, 0.25f, 0.0f // top left
    };

    unsigned int indices[] = {  // note that we start from 0!
        0, 1, 2,   // first triangle
        0, 2, 3
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

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
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

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);  

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    // read position 0(location), for each 3 floats, skip 3 * sizeof float.
    
    glEnableVertexAttribArray(0); 

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // classic CPU style

    int x_diff = 0, y_diff = 0;
    int rotate = 0;

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

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
            rotate = (rotate + 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            rotate = (rotate - 1) % 360;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        int offsetLoc = glGetUniformLocation(shaderProgram, "offset");
        float degLoc = glGetUniformLocation(shaderProgram, "deg");
        // int x_pos = x_diff + 
        glUniform2f(offsetLoc, x_diff / 100.0f, y_diff / 100.0f);
        glUniform1f(degLoc, (float)rotate);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * 12, vertices);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}