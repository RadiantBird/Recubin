#pragma once

#include <Core/IInputBackend.hpp>
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// SDL3 input provider. SDLInputBackend::processEvent must be called for every
// SDL event before User consumes input for the frame.
class SDLInputBackend final : public IInputBackend {
public:
    explicit SDLInputBackend(SDL_Window* window);
    ~SDLInputBackend() override;

    SDLInputBackend(const SDLInputBackend&) = delete;
    SDLInputBackend& operator=(const SDLInputBackend&) = delete;

    bool isKeyDown(KeyCode key) const;
    bool isMouseButtonDown(MouseButton button) const;
    void getCursorPos(double& x, double& y) const;
    void setCursorPos(double x, double y) override;
    void setMouseCaptured(bool captured);
    bool setCustomCursor(const CursorImageData& image);
    double consumeScrollDelta() override;
    bool consumeRelativeMouseDelta(double& x, double& y) override;
    bool usesRelativeMouseInput() const override { return m_captured; }

    static bool processEvent(const SDL_Event& event);

private:
    static SDL_Scancode toScancode(KeyCode key);
    static Uint8 toMouseButton(MouseButton button);
    void resetMotion();

    SDL_Window* m_window = nullptr;
    SDL_WindowID m_windowId = 0;
    double m_pendingScrollY = 0.0;
    double m_pendingRelativeX = 0.0;
    double m_pendingRelativeY = 0.0;
    bool m_captured = false;
    SDL_Cursor* m_activeCursor = nullptr;
    std::array<SDL_Cursor*, 10> m_cursorCache{};
    std::array<std::uint64_t, 10> m_cursorRevisions{};
    std::unordered_set<std::uint64_t> m_failedCursorRevisions;

    static std::unordered_map<SDL_WindowID, SDLInputBackend*> s_instances;
};
