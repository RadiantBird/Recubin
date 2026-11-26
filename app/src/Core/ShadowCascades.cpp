#include <Core/ShadowCascades.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

std::optional<ShadowCascadeSet> buildShadowCascades(
    float shadowDistance, float fovYDegrees, float aspect,
    const Vector3& cameraPosition, const Vector3& cameraForwardInput,
    const Vector3& cameraUpInput, const Vector3& lightDirectionInput) {
    constexpr float CAMERA_NEAR = 0.1f;
    constexpr float CASCADE_SPLIT_LAMBDA = 0.7f;
    constexpr float CASCADE_XY_MARGIN = 8.0f;
    constexpr float CASCADE_DEPTH_MARGIN = 32.0f;
    constexpr float SHADOW_MAP_SIZE = 2048.0f;
    if (!std::isfinite(shadowDistance) || shadowDistance <= CAMERA_NEAR ||
        !std::isfinite(fovYDegrees) || fovYDegrees <= 0.0f ||
        !std::isfinite(aspect) || aspect <= 0.0f) {
        return std::nullopt;
    }
    const Vector3 cameraForward = cameraForwardInput.normalize();
    const Vector3 cameraRight = Vector3::Cross(cameraForward, cameraUpInput).normalize();
    const Vector3 cameraUp = Vector3::Cross(cameraRight, cameraForward).normalize();
    const Vector3 lightDirection = lightDirectionInput.normalize();
    if (!std::isfinite(cameraForward.length()) || cameraForward.length() < 0.5f ||
        !std::isfinite(cameraRight.length()) || cameraRight.length() < 0.5f ||
        !std::isfinite(cameraUp.length()) || cameraUp.length() < 0.5f ||
        !std::isfinite(lightDirection.length()) || lightDirection.length() < 0.5f) {
        return std::nullopt;
    }
    const Vector3 lightUpReference = std::abs(lightDirection.y) < 0.99f
        ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
    const Vector3 lightRight = Vector3::Cross(lightDirection, lightUpReference).normalize();
    const Vector3 lightUp = Vector3::Cross(lightRight, lightDirection).normalize();
    if (lightRight.length() < 0.5f || lightUp.length() < 0.5f) return std::nullopt;

    ShadowCascadeSet result;
    const float range = shadowDistance - CAMERA_NEAR;
    for (int cascade = 0; cascade < 2; ++cascade) {
        const float fraction = static_cast<float>(cascade + 1) / 3.0f;
        const float linear = CAMERA_NEAR + range * fraction;
        const float logarithmic = CAMERA_NEAR * std::pow(shadowDistance / CAMERA_NEAR, fraction);
        result.splits[cascade] = linear * (1.0f - CASCADE_SPLIT_LAMBDA) +
                                 logarithmic * CASCADE_SPLIT_LAMBDA;
    }
    result.splits[2] = shadowDistance;
    const float firstRange = result.splits[0] - CAMERA_NEAR;
    const float secondRange = result.splits[1] - result.splits[0];
    result.blendWidths[0] = std::min(std::max(firstRange * 0.08f, 0.25f), firstRange * 0.45f);
    result.blendWidths[1] = std::min(std::max(secondRange * 0.08f, 0.25f), secondRange * 0.45f);
    const float tanHalfFov = std::tan(fovYDegrees * 3.14159265358979323846f / 360.0f);
    for (int cascade = 0; cascade < 3; ++cascade) {
        const float sliceNear = cascade == 0 ? CAMERA_NEAR : result.splits[cascade - 1];
        const float sliceFar = result.splits[cascade];
        const Vector3 nearCenter = cameraPosition + cameraForward * sliceNear;
        const Vector3 farCenter = cameraPosition + cameraForward * sliceFar;
        const float nearHalfHeight = tanHalfFov * sliceNear;
        const float farHalfHeight = tanHalfFov * sliceFar;
        const float nearHalfWidth = nearHalfHeight * aspect;
        const float farHalfWidth = farHalfHeight * aspect;
        const std::array<Vector3, 8> corners = {
            nearCenter - cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
            nearCenter + cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
            nearCenter - cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
            nearCenter + cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
            farCenter - cameraRight * farHalfWidth - cameraUp * farHalfHeight,
            farCenter + cameraRight * farHalfWidth - cameraUp * farHalfHeight,
            farCenter - cameraRight * farHalfWidth + cameraUp * farHalfHeight,
            farCenter + cameraRight * farHalfWidth + cameraUp * farHalfHeight};
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minDepth = std::numeric_limits<float>::max();
        float maxDepth = std::numeric_limits<float>::lowest();
        for (const Vector3& corner : corners) {
            minX = std::min(minX, Vector3::Dot(lightRight, corner));
            maxX = std::max(maxX, Vector3::Dot(lightRight, corner));
            minY = std::min(minY, Vector3::Dot(lightUp, corner));
            maxY = std::max(maxY, Vector3::Dot(lightUp, corner));
            minDepth = std::min(minDepth, Vector3::Dot(lightDirection, corner));
            maxDepth = std::max(maxDepth, Vector3::Dot(lightDirection, corner));
        }
        const float halfWidth = std::max((maxX - minX) * 0.5f + CASCADE_XY_MARGIN, 0.001f);
        const float halfHeight = std::max((maxY - minY) * 0.5f + CASCADE_XY_MARGIN, 0.001f);
        const float texelWidth = 2.0f * halfWidth / SHADOW_MAP_SIZE;
        const float texelHeight = 2.0f * halfHeight / SHADOW_MAP_SIZE;
        const float centerX = std::round(((maxX + minX) * 0.5f) / texelWidth) * texelWidth;
        const float centerY = std::round(((maxY + minY) * 0.5f) / texelHeight) * texelHeight;
        const float lightNear = 0.1f;
        const float lightFar = std::max(lightNear + 1.0f,
                                        maxDepth - minDepth + CASCADE_DEPTH_MARGIN * 2.0f);
        const Vector3 lightEye = lightRight * centerX + lightUp * centerY +
                                 lightDirection * (minDepth - CASCADE_DEPTH_MARGIN);
        const Matrix4 view = Matrix4::LookAt(lightEye, lightEye + lightDirection, lightUpReference);
        result.lightSpaceMatrices[cascade] = Matrix4::Ortho(-halfWidth, halfWidth,
            -halfHeight, halfHeight, lightNear, lightFar) * view;
    }
    return result;
}
