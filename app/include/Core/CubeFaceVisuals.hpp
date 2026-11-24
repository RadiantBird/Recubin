#pragma once

#include <array>
#include <string>

#include <Math/Vector2.hpp>
#include <Util/Color4.hpp>

class Cube;

struct CubeFaceVisual {
    std::string path;
    Color4 tint;
    Vector2 uvScale{1.0f, 1.0f};
    bool decal = false;
};

struct CubeFaceVisuals {
    std::array<CubeFaceVisual, 6> direct;
    std::array<CubeFaceVisual, 6> material;
};

CubeFaceVisuals collectCubeFaceVisuals(Cube& cube);
