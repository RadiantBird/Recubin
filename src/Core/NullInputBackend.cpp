#include <Core/NullInputBackend.hpp>

bool NullInputBackend::isKeyDown(KeyCode key) const { return false; }
bool NullInputBackend::isMouseButtonDown(MouseButton button) const { return false; }
void NullInputBackend::getCursorPos(double& x, double& y) const { x = 0.0; y = 0.0; }
void NullInputBackend::setCursorPos(double x, double y) {}
void NullInputBackend::setMouseCaptured(bool captured) {}
bool NullInputBackend::setCustomCursor(const std::string&, int, int) { return false; }
double NullInputBackend::consumeScrollDelta() { return 0.0; }
