#include <Core/GLFWInputBackend.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
#include <include/stb_image.h>
#include <include/Util/Logger.hpp>
#include <algorithm>

GLFWInputBackend* GLFWInputBackend::s_instance = nullptr;

static int toGlfwKey(KeyCode key) {
    switch (key) {
        case KeyCode::A: return GLFW_KEY_A;
        case KeyCode::B: return GLFW_KEY_B;
        case KeyCode::C: return GLFW_KEY_C;
        case KeyCode::D: return GLFW_KEY_D;
        case KeyCode::E: return GLFW_KEY_E;
        case KeyCode::F: return GLFW_KEY_F;
        case KeyCode::G: return GLFW_KEY_G;
        case KeyCode::H: return GLFW_KEY_H;
        case KeyCode::I: return GLFW_KEY_I;
        case KeyCode::J: return GLFW_KEY_J;
        case KeyCode::K: return GLFW_KEY_K;
        case KeyCode::L: return GLFW_KEY_L;
        case KeyCode::M: return GLFW_KEY_M;
        case KeyCode::N: return GLFW_KEY_N;
        case KeyCode::O: return GLFW_KEY_O;
        case KeyCode::P: return GLFW_KEY_P;
        case KeyCode::Q: return GLFW_KEY_Q;
        case KeyCode::R: return GLFW_KEY_R;
        case KeyCode::S: return GLFW_KEY_S;
        case KeyCode::T: return GLFW_KEY_T;
        case KeyCode::U: return GLFW_KEY_U;
        case KeyCode::V: return GLFW_KEY_V;
        case KeyCode::W: return GLFW_KEY_W;
        case KeyCode::X: return GLFW_KEY_X;
        case KeyCode::Y: return GLFW_KEY_Y;
        case KeyCode::Z: return GLFW_KEY_Z;
        case KeyCode::Num0: return GLFW_KEY_0;
        case KeyCode::Num1: return GLFW_KEY_1;
        case KeyCode::Num2: return GLFW_KEY_2;
        case KeyCode::Num3: return GLFW_KEY_3;
        case KeyCode::Num4: return GLFW_KEY_4;
        case KeyCode::Num5: return GLFW_KEY_5;
        case KeyCode::Num6: return GLFW_KEY_6;
        case KeyCode::Num7: return GLFW_KEY_7;
        case KeyCode::Num8: return GLFW_KEY_8;
        case KeyCode::Num9: return GLFW_KEY_9;
        case KeyCode::Up:    return GLFW_KEY_UP;
        case KeyCode::Down:  return GLFW_KEY_DOWN;
        case KeyCode::Left:  return GLFW_KEY_LEFT;
        case KeyCode::Right: return GLFW_KEY_RIGHT;
        case KeyCode::Escape:       return GLFW_KEY_ESCAPE;
        case KeyCode::Space:        return GLFW_KEY_SPACE;
        case KeyCode::Enter:        return GLFW_KEY_ENTER;
        case KeyCode::Tab:          return GLFW_KEY_TAB;
        case KeyCode::Backspace:    return GLFW_KEY_BACKSPACE;
        case KeyCode::LeftShift:    return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::RightShift:   return GLFW_KEY_RIGHT_SHIFT;
        case KeyCode::LeftControl:  return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::RightControl: return GLFW_KEY_RIGHT_CONTROL;
        case KeyCode::LeftAlt:      return GLFW_KEY_LEFT_ALT;
        case KeyCode::RightAlt:     return GLFW_KEY_RIGHT_ALT;
        case KeyCode::F1: return GLFW_KEY_F1;
        case KeyCode::F2: return GLFW_KEY_F2;
        case KeyCode::F3: return GLFW_KEY_F3;
        case KeyCode::F4: return GLFW_KEY_F4;
        case KeyCode::F5: return GLFW_KEY_F5;
        case KeyCode::F6: return GLFW_KEY_F6;
        case KeyCode::F7: return GLFW_KEY_F7;
        case KeyCode::F8: return GLFW_KEY_F8;
        case KeyCode::F9: return GLFW_KEY_F9;
        case KeyCode::F10: return GLFW_KEY_F10;
        case KeyCode::F11: return GLFW_KEY_F11;
        case KeyCode::F12: return GLFW_KEY_F12;
        default: return GLFW_KEY_UNKNOWN;
    }
}

static int toGlfwMouseButton(MouseButton button) {
    switch (button) {
        case MouseButton::Left:   return GLFW_MOUSE_BUTTON_LEFT;
        case MouseButton::Right:  return GLFW_MOUSE_BUTTON_RIGHT;
        case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        default: return GLFW_MOUSE_BUTTON_LEFT;
    }
}

void GLFWInputBackend::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (s_instance && s_instance->m_window == window) {
        s_instance->m_pendingScrollY += yoffset;
        if (s_instance->m_previousScrollCallback) {
            s_instance->m_previousScrollCallback(window, xoffset, yoffset);
        }
    }
}

GLFWInputBackend::GLFWInputBackend(GLFWwindow* window)
    : m_window(window)
{
    s_instance = this;
    if (m_window) {
        // 既存のスクロールコールバック(ImGui 等)を保持して連鎖させる
        m_previousScrollCallback = glfwSetScrollCallback(m_window, &GLFWInputBackend::scrollCallback);
    }
}

GLFWInputBackend::~GLFWInputBackend() {
    for (auto& entry : m_cursorCache) {
        if (entry.cursor) glfwDestroyCursor(entry.cursor);
        entry.cursor = nullptr;
    }
    if (s_instance == this) s_instance = nullptr;
}

bool GLFWInputBackend::isKeyDown(KeyCode key) const {
    if (!m_window) return false;
    int glfwKey = toGlfwKey(key);
    if (glfwKey == GLFW_KEY_UNKNOWN) return false;
    return glfwGetKey(m_window, glfwKey) == GLFW_PRESS;
}

bool GLFWInputBackend::isMouseButtonDown(MouseButton button) const {
    if (!m_window) return false;
    return glfwGetMouseButton(m_window, toGlfwMouseButton(button)) == GLFW_PRESS;
}

void GLFWInputBackend::getCursorPos(double& x, double& y) const {
    x = 0.0; y = 0.0;
    if (m_window) glfwGetCursorPos(m_window, &x, &y);
}

void GLFWInputBackend::setCursorPos(double x, double y) {
    if (m_window) glfwSetCursorPos(m_window, x, y);
}

void GLFWInputBackend::setMouseCaptured(bool captured) {
    if (!m_window) return;
    if (captured) {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    } else {
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

bool GLFWInputBackend::setCustomCursor(const std::string& path, int hotspotX, int hotspotY) {
    if (!m_window) return false;
    const std::string normalized = AssetPath::normalize(path);
    if (normalized.empty()) {
        m_activeCursor = nullptr;
        glfwSetCursor(m_window, nullptr);
        return true;
    }
    if (!AssetGuard::allow(normalized)) return false;
    std::error_code ec;
    const auto fsPath = AssetPath::fromStored(normalized);
    const auto writeTime = std::filesystem::last_write_time(fsPath, ec);
    if (ec) return false;
    for (auto& entry : m_cursorCache) {
        if (entry.cursor && entry.path == normalized && entry.writeTime == writeTime &&
            entry.hotspotX == hotspotX && entry.hotspotY == hotspotY) {
            m_activeCursor = entry.cursor;
            glfwSetCursor(m_window, m_activeCursor);
            return true;
        }
    }
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(0);
    stbi_uc* pixels = stbi_load(fsPath.string().c_str(), &width, &height, &channels, 4);
    stbi_set_flip_vertically_on_load(1);
    if (!pixels || width <= 0 || height <= 0) {
        const std::string warningKey = normalized + ":" + std::to_string(hotspotX) + ":" + std::to_string(hotspotY);
        if (m_cursorWarningKeys.insert(warningKey).second) {
            RCBN_WARN("GLFWInputBackend: failed to load cursor image " + normalized);
        }
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    GLFWimage image{width, height, pixels};
    const int clampedX = std::clamp(hotspotX, 0, width - 1);
    const int clampedY = std::clamp(hotspotY, 0, height - 1);
    GLFWcursor* cursor = glfwCreateCursor(&image, clampedX, clampedY);
    stbi_image_free(pixels);
    if (!cursor) return false;
    CursorCacheEntry* target = nullptr;
    for (auto& entry : m_cursorCache) if (!entry.cursor) { target = &entry; break; }
    if (!target) target = &m_cursorCache[0];
    if (target->cursor) glfwDestroyCursor(target->cursor);
    target->cursor = cursor;
    target->path = normalized;
    target->writeTime = writeTime;
    target->hotspotX = hotspotX;
    target->hotspotY = hotspotY;
    m_activeCursor = cursor;
    glfwSetCursor(m_window, m_activeCursor);
    return true;
}

double GLFWInputBackend::consumeScrollDelta() {
    double v = m_pendingScrollY;
    m_pendingScrollY = 0.0;
    return v;
}
