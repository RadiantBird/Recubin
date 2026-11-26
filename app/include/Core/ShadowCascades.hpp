#pragma once

#include <Math/Matrix4.hpp>
#include <Math/Vector3.hpp>

#include <array>
#include <optional>

struct ShadowCascadeSet {
    std::array<Matrix4, 3> lightSpaceMatrices{};
    std::array<float, 3> splits{};
    std::array<float, 2> blendWidths{};
};

std::optional<ShadowCascadeSet> buildShadowCascades(
    float shadowDistance,
    float fovYDegrees,
    float aspect,
    const Vector3& cameraPosition,
    const Vector3& cameraForward,
    const Vector3& cameraUp,
    const Vector3& lightDirection);
