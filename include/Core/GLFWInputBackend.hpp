#pragma once

#include <Core/IInputBackend.hpp>
#include <include/GLFW/glfw3.h>
#include <filesystem>
#include <string>
#include <array>
#include <unordered_set>

// ==================================================================
//  GLFWInputBackend
//
//  GLFW を用いた IInputBackend の実装。KeyCode/MouseButton を GLFW の
//  定数へ変換して入力を読み取る。スクロールはコールバックで蓄積し、
//  既存のスクロールコールバック(ImGui 等)へ連鎖させる。
// ==================================================================
class GLFWInputBackend : public IInputBackend {
public:
    explicit GLFWInputBackend(GLFWwindow* window);
    ~GLFWInputBackend() override;

    bool isKeyDown(KeyCode key) const override;
    bool isMouseButtonDown(MouseButton button) const override;
    void getCursorPos(double& x, double& y) const override;
    void setCursorPos(double x, double y) override;
    void setMouseCaptured(bool captured) override;
    bool setCustomCursor(const std::string& path, int hotspotX, int hotspotY) override;
    double consumeScrollDelta() override;

private:
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    GLFWwindow*    m_window;
    double         m_pendingScrollY = 0.0;
    GLFWscrollfun  m_previousScrollCallback = nullptr;
    struct CursorCacheEntry {
        GLFWcursor* cursor = nullptr;
        std::string path;
        std::filesystem::file_time_type writeTime{};
        int hotspotX = 0;
        int hotspotY = 0;
    };
    std::array<CursorCacheEntry, 10> m_cursorCache{};
    GLFWcursor* m_activeCursor = nullptr;
    std::unordered_set<std::string> m_cursorWarningKeys;

    static GLFWInputBackend* s_instance;
};
