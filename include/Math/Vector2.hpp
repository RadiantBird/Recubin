#pragma once
#include <string>

struct Vector2 {
    float x = 0.f, y = 0.f;

    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& v) const { return {x + v.x, y + v.y}; }
    Vector2 operator-(const Vector2& v) const { return {x - v.x, y - v.y}; }
    Vector2 operator-() const { return {-x, -y}; }
    Vector2 operator*(float s) const { return {x * s, y * s}; }
    Vector2 operator*(const Vector2& v) const { return {x * v.x, y * v.y}; }
    Vector2 operator/(float s) const { return {x / s, y / s}; }
    Vector2 operator/(const Vector2& v) const { return {x / v.x, y / v.y}; }
    bool operator==(const Vector2& v) const { return x == v.x && y == v.y; }

    std::string toString() const {
        return std::to_string(x) + ", " + std::to_string(y);
    }
};

enum class Norm { Pixel, Scale };
