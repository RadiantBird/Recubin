#pragma once

#include <include/Math/Matrix4.hpp>
#include <include/Math/Quaternion.hpp>
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Cube.hpp>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <cmath>

struct camera {
    Quaternion Orientation; // 角度の代わりに姿勢を保持
    Vector3 Position = Vector3(0, -2, 5);
};

class User {
public:
    GLFWwindow* window;

    float speed = 0.05f;
    float rotationSpeed = 1.0f; // 回転の速さ
    camera current_camera;
    
    camera &cam;
    Vector3 &cpos;
    Model *character = nullptr;
    Cube *torso = nullptr;
    Cube *head = nullptr;
    Cube *leftArm = nullptr;
    Cube *rightArm = nullptr;
    Cube *leftLeg = nullptr;
    Cube *rightLeg = nullptr;

    enum class ControlMode {
        Free,
        Character
    } controlMode = ControlMode::Free;

    Vector3 forward;
    Vector3 right;
    Vector3 up;

    bool wannaExit = false;

    User(GLFWwindow* window);

    void updateVectors();
    void processInput();
    void spawnCharacter();
};