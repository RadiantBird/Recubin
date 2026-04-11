#include <include/Core/Renderer.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "include/stb_image.h"

void Renderer::createWhiteTexture() {
    glGenTextures(1, &whiteTexture);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    
    // 1x1の白いピクセルデータ（RGBA）
    unsigned char white[] = { 255, 255, 255, 255 }; 
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

std::string Renderer::loadShaderSource(const char* filePath) {
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

void Renderer::init() {
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

    // 2: TexCoord
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
    stbi_set_flip_vertically_on_load(true); // OpenGL用
}

void Renderer::render(User &user, GLFWwindow* window, Workspace &workspace) {
    // 1. プロジェクションとビュー行列は全オブジェクト共通なので先にセット
    Matrix4 projection = Matrix4::Perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);
    Vector3 target = user.cpos + user.forward; 
    Matrix4 view = Matrix4::LookAt(user.cpos, target, user.up);

    glUseProgram(shaderProgram); // 念のため
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection.m);

    // ライティング等の共通パラメータ
    glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), user.cpos.x, user.cpos.y, user.cpos.z);

    int modelLoc = glGetUniformLocation(shaderProgram, "model");

    glBindVertexArray(VAO);

    // 2. 個別のオブジェクトを描画
    for (auto const& [name, child] : workspace.getChildren()) {
        if (child->IsA("Cube")) {
            Cube* cube = static_cast<Cube*>(child);

            // 1. スケール行列 (Sizeをそのまま使う)
            Matrix4 scale = Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);

            // 2. 回転行列 (さっき追加したFromQuaternion)
            Matrix4 rotation = Matrix4::FromQuaternion(cube->Rotation);

            // 3. 移動行列 (Position)
            Matrix4 translation = Matrix4::Translate(cube->Position.x, cube->Position.y, cube->Position.z);

            // 正しくはT * R * S
            Matrix4 modelMat = translation * rotation * scale;

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
            
            // 描画実行
            cube->draw(modelLoc, shaderProgram);
        }
    }
    
    glfwSwapBuffers(window); // 背面バッファを表面に出す
    glfwPollEvents();
}

unsigned int Renderer::loadTexture(const char* path) {
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