#pragma once
#include <string>

struct Vector2 {
    float x = 0.f, y = 0.f;

    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    std::string toString() const {
        return std::to_string(x) + ", " + std::to_string(y);
    }
};

enum class Norm { Pixel, Scale };
