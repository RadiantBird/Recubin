#pragma once
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <include/Math/Matrix4.hpp>

#include <include/Core/User.hpp>
#include <include/Instances/Cube.hpp>
#include <include/Instances/Workspace.hpp>

#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

class Renderer {
    public:
        static Renderer* instance;
        unsigned int VBO;
        unsigned int VAO;
        unsigned int EBO;

        unsigned int shaderProgram;

        std::vector<unsigned int> indices = {};

        std::map<std::string, unsigned int> textureCache;

        unsigned int whiteTexture;
        void createWhiteTexture();

        std::string loadShaderSource(const char* filePath);

        void init();
        virtual ~Renderer();

        void render(User &user, GLFWwindow* window, Workspace &workspace);

        unsigned int loadTexture(const char* path);
};