#pragma once

#include <include/Math/Matrix4.hpp>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <cmath>

struct camera {
    int rotateX = 0, rotateY = 0, rotateZ = 0;
    Vector3 Position = Vector3(0, 0, 5);
};

class User {
public:
    GLFWwindow* window;

    float speed = 0.05f;
    camera current_camera;
    
    // 参照メンバ
    camera &cam;
    Vector3 &cpos;

    Vector3 forward;
    Vector3 right;
    Vector3 up;

    bool wannaExit = false;

    // コンストラクタ
    User(GLFWwindow* window);

    // メソッド
    void updateVectors();
    void processInput();
};