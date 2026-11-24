#pragma once

#include <array>

struct SdlLocalLight {
    std::array<float, 4> positionAndRange{};
    std::array<float, 4> directionAndCutoff{};
    std::array<float, 4> radiance{};
};

struct SdlMaterialLighting {
    std::array<float, 4> cameraAndLightCount{};
    std::array<float, 4> sunDirection{};
    std::array<float, 4> sunIrradiance{};
    std::array<SdlLocalLight, 8> lights{};
};
