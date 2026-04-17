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
    float walkPower = 5.0f;
    float rotationSpeed = 1.0f; // 回転の速さ
    float cameraDistance = 10.0f; // カメラの距離
    float zoomSpeed = 0.1f;
    float walkCycle = 0.0f; // 歩行アニメーション用
    Vector3 currentMoveDir; // 滑らかな移動・回転用
    bool lastFKeyPressed = false; // トグル判定用
    camera current_camera;
    
    camera &cam;
    Vector3 &cpos;
    Model *character = nullptr;
    Cube *root = nullptr; // 追加: キャラクターのルート（全体をまとめる）
    // Cube *torso = nullptr; // TODO: あとで実装
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
    ~User();

    void updateVectors();
    void processInput();
    void spawnCharacter();
};