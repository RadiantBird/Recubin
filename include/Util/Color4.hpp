#pragma once

struct Color4 {
    float r, g, b, a;

    Color4(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}

    // 0-255
    static Color4 FromRGB(int r, int g, int b, float a = 1.0f) {
        return Color4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    }
};