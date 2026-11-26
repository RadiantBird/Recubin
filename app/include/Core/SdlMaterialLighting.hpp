#pragma once

#include <array>

struct SdlLocalLight {
    std::array<float, 4> positionAndRange{};
    std::array<float, 4> directionAndCutoff{};
    std::array<float, 4> radiance{};
};

struct SdlShadowCascades {
    std::array<std::array<float, 16>, 3> lightSpaceMatrices{};
    std::array<float, 3> splits{};
    std::array<float, 2> blendWidths{};
    bool valid = false;
};

struct SdlMaterialLighting {
    std::array<float, 4> cameraAndLightCount{};
    std::array<float, 4> sunDirection{};
    std::array<float, 4> sunIrradiance{};
    std::array<SdlLocalLight, 8> lights{};
    float shadowDistance = 160.0f;
    float shadowFadeDistance = 20.0f;
    SdlShadowCascades shadowCascades{};
};
