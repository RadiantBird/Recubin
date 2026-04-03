#include <include/Core/User.hpp>

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
    float radX = cam.rotateX * pi / 180.0f;
    float radY = cam.rotateY * pi / 180.0f;

    forward = Vector3(
        sin(radY) * cos(radX),
        -sin(radX),
        -cos(radY) * cos(radX)
    ).normalize();

    right = Vector3::Cross(forward, Vector3(0, 1, 0)).normalize();
    up = Vector3::Cross(right, forward).normalize();
}

void User::processInput() {
    if (!window) return;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        cpos = cpos + forward * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        cpos = cpos - forward * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        cpos = cpos - right * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        cpos = cpos + right * speed;
    }
    
    // 上下移動
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        cpos.y += speed;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        cpos.y -= speed;
    }

    // 回転操作
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  cam.rotateY = (cam.rotateY + 1) % 360;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) cam.rotateY = (cam.rotateY - 1) % 360;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)    cam.rotateX = (cam.rotateX + 1) % 360;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)  cam.rotateX = (cam.rotateX - 1) % 360;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wannaExit = true;
    }
}