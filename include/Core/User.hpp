#pragma once

#include <include/Math/Matrix4.hpp>
#include <include/Math/Quaternion.hpp>
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/Sphere.hpp>
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

    float speed = 0.25f;
    float walkPower = 5.0f;
    float jumpPower = 7.0f;
    float rotationSpeed = 1.0f; // 回転の速さ
    float cameraDistance = 10.0f; // カメラの距離
    float zoomSpeed = 0.1f;
    float mouseZoomSpeed = 1.0f; // キーボードより早めに
    float walkCycle = 0.0f; // 歩行アニメーション用
    Vector3 currentMoveDir; // 滑らかな移動・回転用
    bool lastFKeyPressed = false; // トグル判定用
    bool allowControlModeSwitch = true; // false のとき L キーをブロック
    bool lastSpacePressed = false; // ジャンプ用
    bool isGrounded = true; // 接地状態
    camera current_camera;
    
    camera &cam;
    Vector3 &cpos;
    std::shared_ptr<Model> character = nullptr;
    std::shared_ptr<Cube> root = nullptr; // キャラクターのルート（全体をまとめる）
    std::shared_ptr<Cube> torso = nullptr;
    std::shared_ptr<Sphere> head = nullptr;
    std::shared_ptr<Cube> leftArm = nullptr;
    std::shared_ptr<Cube> rightArm = nullptr;
    std::shared_ptr<Cube> leftLeg = nullptr;
    std::shared_ptr<Cube> rightLeg = nullptr;

    enum class ControlMode {
        Free,
        Character
    } controlMode = ControlMode::Character;

    Vector3 forward;
    Vector3 right;
    Vector3 up;

    bool wannaExit = false;
    bool wantsSwitchWorkspace = false;
    bool isRightMouseRotating = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    double pendingScrollY = 0.0;
    bool isScrollCallbackInstalled = false;
    GLFWscrollfun previousScrollCallback = nullptr;

    User(GLFWwindow* window);
    ~User();

    void updateVectors();
    void processInput(class Physics& physics);
    void spawnCharacter(class CharacterSetting* cs = nullptr);
    void despawnCharacter();
    static User* getInstance() { return s_instance; }

private:
    static User* s_instance;
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};
