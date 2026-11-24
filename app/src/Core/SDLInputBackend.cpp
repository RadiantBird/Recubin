#include <Core/SDLInputBackend.hpp>
#include <Util/Logger.hpp>

#include <cstring>

std::unordered_map<SDL_WindowID, SDLInputBackend*> SDLInputBackend::s_instances;

SDLInputBackend::SDLInputBackend(SDL_Window* window) {
    if (!window) {
        RCBN_ERROR("SDLInputBackend: cannot attach to a null SDL_Window");
        return;
    }
    setWindow(window);
}

SDLInputBackend::~SDLInputBackend() {
    setWindow(nullptr);
    for (SDL_Cursor*& cursor : m_cursorCache) {
        if (cursor) SDL_DestroyCursor(cursor);
        cursor = nullptr;
    }
    m_activeCursor = nullptr;
}

bool SDLInputBackend::setWindow(SDL_Window* window) {
    const SDL_WindowID nextId = window ? SDL_GetWindowID(window) : 0;
    if (window && nextId == 0) {
        RCBN_ERROR("SDLInputBackend: SDL_GetWindowID failed: " << SDL_GetError());
        return false;
    }
    if (nextId != 0) {
        const auto existing = s_instances.find(nextId);
        if (existing != s_instances.end() && existing->second != this) {
            RCBN_ERROR("SDLInputBackend: duplicate window id " << nextId);
            return false;
        }
    }
    if (nextId == m_windowId) return true;

    if (m_windowId != 0) {
        const auto registered = s_instances.find(m_windowId);
        if (registered != s_instances.end() && registered->second == this) {
            s_instances.erase(registered);
        }
        applyMouseCapture(false);
    }
    m_windowId = 0;
    m_captured = false;
    resetMotion();
    m_pendingScrollY = 0.0;

    if (!window) return true;
    m_windowId = nextId;
    s_instances.emplace(m_windowId, this);
    applyMouseCapture(SDL_GetKeyboardFocus() == window);
    return true;
}

SDL_Scancode SDLInputBackend::toScancode(KeyCode key) {
    if (key >= KeyCode::A && key <= KeyCode::Z) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + static_cast<int>(key) - static_cast<int>(KeyCode::A));
    }
    if (key >= KeyCode::Num1 && key <= KeyCode::Num9) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + static_cast<int>(key) - static_cast<int>(KeyCode::Num1));
    }
    if (key >= KeyCode::F1 && key <= KeyCode::F12) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + static_cast<int>(key) - static_cast<int>(KeyCode::F1));
    }
    switch (key) {
        case KeyCode::Num0: return SDL_SCANCODE_0;
        case KeyCode::Up: return SDL_SCANCODE_UP;
        case KeyCode::Down: return SDL_SCANCODE_DOWN;
        case KeyCode::Left: return SDL_SCANCODE_LEFT;
        case KeyCode::Right: return SDL_SCANCODE_RIGHT;
        case KeyCode::Escape: return SDL_SCANCODE_ESCAPE;
        case KeyCode::Space: return SDL_SCANCODE_SPACE;
        case KeyCode::Enter: return SDL_SCANCODE_RETURN;
        case KeyCode::Tab: return SDL_SCANCODE_TAB;
        case KeyCode::Backspace: return SDL_SCANCODE_BACKSPACE;
        case KeyCode::LeftShift: return SDL_SCANCODE_LSHIFT;
        case KeyCode::RightShift: return SDL_SCANCODE_RSHIFT;
        case KeyCode::LeftControl: return SDL_SCANCODE_LCTRL;
        case KeyCode::RightControl: return SDL_SCANCODE_RCTRL;
        case KeyCode::LeftAlt: return SDL_SCANCODE_LALT;
        case KeyCode::RightAlt: return SDL_SCANCODE_RALT;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

Uint8 SDLInputBackend::toMouseButton(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return SDL_BUTTON_LEFT;
        case MouseButton::Right: return SDL_BUTTON_RIGHT;
        case MouseButton::Middle: return SDL_BUTTON_MIDDLE;
        default: return SDL_BUTTON_LEFT;
    }
}

bool SDLInputBackend::isKeyDown(KeyCode key) const {
    auto* focus = SDL_GetKeyboardFocus();
    if (!focus || SDL_GetWindowID(focus) != m_windowId) {
        return false;
    }
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    const SDL_Scancode code = toScancode(key);
    return state && code != SDL_SCANCODE_UNKNOWN && code < count && state[code];
}

bool SDLInputBackend::isMouseButtonDown(MouseButton button) const {
    auto* focus = SDL_GetMouseFocus();
    if (!focus || SDL_GetWindowID(focus) != m_windowId) {
        return false;
    }
    const SDL_MouseButtonFlags state = SDL_GetMouseState(nullptr, nullptr);
    return (state & SDL_BUTTON_MASK(toMouseButton(button))) != 0;
}

void SDLInputBackend::getCursorPos(double& x, double& y) const {
    float px = 0.0f, py = 0.0f;
    SDL_GetMouseState(&px, &py);
    x = px;
    y = py;
}

void SDLInputBackend::setCursorPos(double x, double y) {
    if (m_windowId == 0) return;
    SDL_Window* window = SDL_GetWindowFromID(m_windowId);
    if (!window) return;
    SDL_WarpMouseInWindow(window, static_cast<float>(x), static_cast<float>(y));
}

void SDLInputBackend::setMouseCaptured(bool captured) {
    m_captureRequested = captured;
    auto* focus = SDL_GetKeyboardFocus();
    applyMouseCapture(focus && SDL_GetWindowID(focus) == m_windowId);
}

void SDLInputBackend::applyMouseCapture(bool focused) {
    const bool captured = m_captureRequested && focused;
    if (captured == m_captured) {
        return;
    }
    SDL_Window* window = SDL_GetWindowFromID(m_windowId);
    if (!window) {
        m_captured = false;
        resetMotion();
        return;
    }
    if (!SDL_SetWindowRelativeMouseMode(window, captured)) {
        RCBN_ERROR("SDLInputBackend: relative mouse mode failed for window " << m_windowId
                   << ": " << SDL_GetError());
        return;
    }
    m_captured = captured;
    resetMotion();
}

bool SDLInputBackend::setCustomCursor(const CursorImageData& imageData) {
    if (!m_windowId || !SDL_GetWindowFromID(m_windowId) || !imageData.isValid()) return false;
    if (m_failedCursorRevisions.contains(imageData.revision)) return false;
    for (std::size_t i = 0; i < m_cursorCache.size(); ++i) {
        if (m_cursorCache[i] && m_cursorRevisions[i] == imageData.revision) {
            m_activeCursor = m_cursorCache[i];
            return SDL_SetCursor(m_activeCursor);
        }
    }
    SDL_Surface* surface = SDL_CreateSurface(imageData.width, imageData.height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        m_failedCursorRevisions.insert(imageData.revision);
        RCBN_ERROR("SDLInputBackend: cursor surface creation failed: " << SDL_GetError());
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(imageData.width) * 4;
    for (int y = 0; y < imageData.height; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch,
                    imageData.rgba.data() + y * rowBytes, rowBytes);
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, imageData.hotspotX, imageData.hotspotY);
    SDL_DestroySurface(surface);
    if (!cursor || !SDL_SetCursor(cursor)) {
        if (cursor) SDL_DestroyCursor(cursor);
        m_failedCursorRevisions.insert(imageData.revision);
        RCBN_ERROR("SDLInputBackend: cursor creation failed: " << SDL_GetError());
        return false;
    }
    std::size_t slot = 0;
    while (slot < m_cursorCache.size() && m_cursorCache[slot]) ++slot;
    if (slot == m_cursorCache.size()) slot = 0;
    if (m_cursorCache[slot]) SDL_DestroyCursor(m_cursorCache[slot]);
    m_cursorCache[slot] = cursor;
    m_cursorRevisions[slot] = imageData.revision;
    m_activeCursor = cursor;
    return true;
}

double SDLInputBackend::consumeScrollDelta() {
    const double value = m_pendingScrollY;
    m_pendingScrollY = 0.0;
    return value;
}

bool SDLInputBackend::consumeRelativeMouseDelta(double& x, double& y) {
    x = m_pendingRelativeX;
    y = m_pendingRelativeY;
    resetMotion();
    return m_captured;
}

void SDLInputBackend::resetMotion() {
    m_pendingRelativeX = 0.0;
    m_pendingRelativeY = 0.0;
}

bool SDLInputBackend::processEvent(const SDL_Event& event) {
    SDL_WindowID windowId = 0;
    if (event.type == SDL_EVENT_MOUSE_MOTION) windowId = event.motion.windowID;
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) windowId = event.wheel.windowID;
    else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
             event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) windowId = event.window.windowID;
    const auto it = s_instances.find(windowId);
    if (it == s_instances.end()) return false;
    SDLInputBackend& backend = *it->second;
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (!backend.m_captured) return false;
            backend.m_pendingRelativeX += event.motion.xrel;
            backend.m_pendingRelativeY += event.motion.yrel;
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            backend.m_pendingScrollY += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                ? -event.wheel.y : event.wheel.y;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            backend.resetMotion();
            backend.m_pendingScrollY = 0.0;
            backend.applyMouseCapture(false);
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            backend.applyMouseCapture(true);
            return true;
        default:
            return false;
    }
}
