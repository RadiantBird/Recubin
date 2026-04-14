#include <Core/User.hpp>

User::User(GLFWwindow* win) 
    : window(win), 
      cam(current_camera), 
      cpos(current_camera.Position),
      forward(0, 0, -1), 
      right(1, 0, 0), 
      up(0, 1, 0) 
{
    updateVectors();
}

void User::updateVectors() {
    // 外積を使わず、クォータニオンから直接ローカル軸を取り出す
    forward = cam.Orientation.getForward();
    right   = cam.Orientation.getRight();
    up      = cam.Orientation.getUp();
}

void User::processInput() {
    if (!window) return;

    // 移動処理（変更なし、updateVectorsの結果を使うので常に最新）
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cpos = cpos + forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cpos = cpos - forward * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cpos = cpos - right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cpos = cpos + right * speed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cpos = cpos - up * speed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cpos = cpos + up * speed;

    bool rotated = false;

    // 回転操作：現在の姿勢に対して「追加の回転」を掛け算する
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        // Y軸（ワールドの上方向）を中心に回転
        cam.Orientation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), rotationSpeed) * cam.Orientation;
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        cam.Orientation = Quaternion::fromAxisAngle(Vector3(0, 1, 0), -rotationSpeed) * cam.Orientation;
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        // X軸（カメラから見て右方向）を中心に回転
        cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1, 0, 0), rotationSpeed);
        rotated = true;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1, 0, 0), -rotationSpeed);
        rotated = true;
    }

    // 回転があった場合のみベクトルを更新
    if (rotated) {
        updateVectors();
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wannaExit = true;
    }
}