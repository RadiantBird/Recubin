#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>

class BaseCube;

struct SdlSurfaceMarkDraw {
    std::string path;
    std::array<float, 16> worldToLocal{};
    std::array<float, 16> projection{};
    std::array<float, 3> size{};
    std::array<float, 4> tint{};
    std::uint32_t texture = 0;
    std::vector<const BaseCube*> targets;
};
