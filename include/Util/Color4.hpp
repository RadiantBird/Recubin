#pragma once

#include <string>

struct Color4 {
    float r, g, b, a;

    Color4(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}

    Color4 operator+(const Color4& c) const { return {r + c.r, g + c.g, b + c.b, a + c.a}; }
    Color4 operator-(const Color4& c) const { return {r - c.r, g - c.g, b - c.b, a - c.a}; }
    Color4 operator-() const { return {-r, -g, -b, -a}; }
    Color4 operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
    Color4 operator*(const Color4& c) const { return {r * c.r, g * c.g, b * c.b, a * c.a}; }
    Color4 operator/(float s) const { return {r / s, g / s, b / s, a / s}; }
    bool operator==(const Color4& c) const { return r == c.r && g == c.g && b == c.b && a == c.a; }

    // 0-255
    static Color4 FromRGB(int r, int g, int b, float a = 1.0f) {
        return Color4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    }

    std::string toString() const {
        return std::to_string(r) + ", " + std::to_string(g) + ", " +
               std::to_string(b) + ", " + std::to_string(a);
    }
};