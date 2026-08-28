#pragma once

#include <Core/IInputBackend.hpp>

// ==================================================================
//  NullInputBackend
//
//  入力を一切供給しないIInputBackend実装。ネットワーク越しのリモートUser
//  (identity表現のみで実際の入力は受け取らない)のUser構築に使う。
// ==================================================================
class NullInputBackend : public IInputBackend {
public:
    bool isKeyDown(KeyCode key) const override;
    bool isMouseButtonDown(MouseButton button) const override;
    void getCursorPos(double& x, double& y) const override;
    void setCursorPos(double x, double y) override;
    void setMouseCaptured(bool captured) override;
    bool setCustomCursor(const CursorImageData& image) override;
    double consumeScrollDelta() override;
};
