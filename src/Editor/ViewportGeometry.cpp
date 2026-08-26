#include <Editor/ViewportGeometry.hpp>

#include <Instances/Spatial.hpp>

#include <algorithm>
#include <cmath>

namespace ViewportGeometry {

Vector3 worldToLocalPosition(const Vector3& worldPosition, const Spatial& spatial) {
    auto parent = spatial.Parent.lock();
    if (parent && parent->IsA("Spatial")) {
        CFrame parentWorld = static_cast<Spatial*>(parent.get())->getWorldCFrame();
        return parentWorld.Rotation.conjugate().rotate(worldPosition - parentWorld.Position);
    }
    return worldPosition;
}

Quaternion worldToLocalRotation(const Quaternion& worldRotation, const Spatial& spatial) {
    auto parent = spatial.Parent.lock();
    if (parent && parent->IsA("Spatial")) {
        CFrame parentWorld = static_cast<Spatial*>(parent.get())->getWorldCFrame();
        return parentWorld.Rotation.conjugate() * worldRotation;
    }
    return worldRotation;
}

Ray makeScreenRay(
    const Vector3& cameraPosition,
    const Vector3& cameraForward,
    const Vector3& cameraRight,
    const Vector3& cameraUp,
    const Vector2& screenPosition,
    const Vector2& viewportSize) {
    const float ndcX = (screenPosition.x / viewportSize.x) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (screenPosition.y / viewportSize.y) * 2.0f;
    const float aspect = (viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        ? viewportSize.x / viewportSize.y
        : 1.0f;
    const float tanHalfFov = std::tan(45.0f * (3.14159265f / 180.0f) * 0.5f);

    Ray ray;
    ray.origin = cameraPosition;
    ray.direction = (cameraForward
        + cameraRight * (ndcX * aspect * tanHalfFov)
        + cameraUp * (ndcY * tanHalfFov)).normalize();
    return ray;
}

ObbRayHit raycastObb(const Ray& ray, const CFrame& worldCFrame, const Vector3& size) {
    const Quaternion inverseRotation = worldCFrame.Rotation.conjugate();
    const Vector3 localOrigin = inverseRotation.rotate(ray.origin - worldCFrame.Position);
    const Vector3 localDirection = inverseRotation.rotate(ray.direction);
    const float boundsMin[3] = { -size.x * 0.5f, -size.y * 0.5f, -size.z * 0.5f };
    const float boundsMax[3] = {  size.x * 0.5f,  size.y * 0.5f,  size.z * 0.5f };
    const float direction[3] = { localDirection.x, localDirection.y, localDirection.z };
    const float origin[3] = { localOrigin.x, localOrigin.y, localOrigin.z };

    float minimumDistance = -1e30f;
    float maximumDistance = 1e30f;
    int axis = 1;
    float sign = 1.0f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(direction[i]) < 1e-8f) {
            if (origin[i] < boundsMin[i] || origin[i] > boundsMax[i]) {
                return {};
            }
        } else {
            float distance1 = (boundsMin[i] - origin[i]) / direction[i];
            float distance2 = (boundsMax[i] - origin[i]) / direction[i];
            const bool swapped = distance1 > distance2;
            if (swapped) {
                std::swap(distance1, distance2);
            }
            if (distance1 > minimumDistance) {
                minimumDistance = distance1;
                axis = i;
                sign = swapped ? 1.0f : -1.0f;
            }
            maximumDistance = (std::min)(maximumDistance, distance2);
            if (maximumDistance < minimumDistance) {
                return {};
            }
        }
    }

    if (maximumDistance < 0.0f) {
        return {};
    }

    ObbRayHit result;
    result.hit = true;
    result.distance = (minimumDistance >= 0.0f) ? minimumDistance : maximumDistance;
    result.axis = axis;
    result.sign = sign;
    return result;
}

float obbSupportRadius(
    const Quaternion& rotation,
    const Vector3& size,
    const Vector3& direction) {
    const Vector3 xAxis = rotation.rotate(Vector3(1.0f, 0.0f, 0.0f));
    const Vector3 yAxis = rotation.rotate(Vector3(0.0f, 1.0f, 0.0f));
    const Vector3 zAxis = rotation.rotate(Vector3(0.0f, 0.0f, 1.0f));
    return std::abs(Vector3::Dot(direction, xAxis)) * size.x * 0.5f
         + std::abs(Vector3::Dot(direction, yAxis)) * size.y * 0.5f
         + std::abs(Vector3::Dot(direction, zAxis)) * size.z * 0.5f;
}

bool obbIntersects(
    const Vector3& positionA,
    const Quaternion& rotationA,
    const Vector3& sizeA,
    const Vector3& positionB,
    const Quaternion& rotationB,
    const Vector3& sizeB) {
    Vector3 axesA[3] = {
        rotationA.rotate(Vector3(1.0f, 0.0f, 0.0f)),
        rotationA.rotate(Vector3(0.0f, 1.0f, 0.0f)),
        rotationA.rotate(Vector3(0.0f, 0.0f, 1.0f))
    };
    Vector3 axesB[3] = {
        rotationB.rotate(Vector3(1.0f, 0.0f, 0.0f)),
        rotationB.rotate(Vector3(0.0f, 1.0f, 0.0f)),
        rotationB.rotate(Vector3(0.0f, 0.0f, 1.0f))
    };
    const Vector3 centerDifference = positionB - positionA;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(Vector3::Dot(axesA[i], centerDifference)) >=
            obbSupportRadius(rotationA, sizeA, axesA[i])
                + obbSupportRadius(rotationB, sizeB, axesA[i])) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (std::abs(Vector3::Dot(axesB[i], centerDifference)) >=
            obbSupportRadius(rotationA, sizeA, axesB[i])
                + obbSupportRadius(rotationB, sizeB, axesB[i])) {
            return false;
        }
    }

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vector3 axis = Vector3::Cross(axesA[i], axesB[j]);
            const float lengthSquared = Vector3::Dot(axis, axis);
            if (lengthSquared < 1e-6f) {
                continue;
            }
            axis = axis.normalize();
            if (std::abs(Vector3::Dot(axis, centerDifference)) >=
                obbSupportRadius(rotationA, sizeA, axis)
                    + obbSupportRadius(rotationB, sizeB, axis)) {
                return false;
            }
        }
    }
    return true;
}

ProjectedPoint projectWorldToScreen(
    const Matrix4& viewProjection,
    const Vector3& worldPosition,
    const Vector2& viewportOrigin,
    const Vector2& viewportSize) {
    const float* matrix = viewProjection.m;
    const float clipX = matrix[0] * worldPosition.x
        + matrix[4] * worldPosition.y
        + matrix[8] * worldPosition.z
        + matrix[12];
    const float clipY = matrix[1] * worldPosition.x
        + matrix[5] * worldPosition.y
        + matrix[9] * worldPosition.z
        + matrix[13];
    const float clipW = matrix[3] * worldPosition.x
        + matrix[7] * worldPosition.y
        + matrix[11] * worldPosition.z
        + matrix[15];

    ProjectedPoint result;
    result.visible = clipW > 0.0f;
    if (!result.visible) {
        return result;
    }
    result.position.x = viewportOrigin.x
        + (clipX / clipW + 1.0f) * 0.5f * viewportSize.x;
    result.position.y = viewportOrigin.y
        + (1.0f - clipY / clipW) * 0.5f * viewportSize.y;
    return result;
}

void accumulateWorldAabb(
    WorldAabb& aabb,
    const CFrame& worldCFrame,
    const Vector3& size) {
    if (!aabb.valid) {
        aabb.minimum = Vector3(1e30f, 1e30f, 1e30f);
        aabb.maximum = Vector3(-1e30f, -1e30f, -1e30f);
    }

    const float halfX = size.x * 0.5f;
    const float halfY = size.y * 0.5f;
    const float halfZ = size.z * 0.5f;
    for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
        const float localX = (cornerIndex & 1) ? halfX : -halfX;
        const float localY = (cornerIndex & 2) ? halfY : -halfY;
        const float localZ = (cornerIndex & 4) ? halfZ : -halfZ;
        const Vector3 worldCorner = worldCFrame.Position
            + worldCFrame.Rotation.rotate(Vector3(localX, localY, localZ));
        aabb.minimum.x = (std::min)(aabb.minimum.x, worldCorner.x);
        aabb.maximum.x = (std::max)(aabb.maximum.x, worldCorner.x);
        aabb.minimum.y = (std::min)(aabb.minimum.y, worldCorner.y);
        aabb.maximum.y = (std::max)(aabb.maximum.y, worldCorner.y);
        aabb.minimum.z = (std::min)(aabb.minimum.z, worldCorner.z);
        aabb.maximum.z = (std::max)(aabb.maximum.z, worldCorner.z);
    }
    aabb.valid = true;
}

Vector3 additiveResize(
    const Vector3& initialSize,
    const Vector3& gizmoScale,
    bool snapEnabled,
    float snapStep,
    float minimumSize) {
    const float initial[3] = { initialSize.x, initialSize.y, initialSize.z };
    const float scale[3] = { gizmoScale.x, gizmoScale.y, gizmoScale.z };
    float result[3] = { initialSize.x, initialSize.y, initialSize.z };
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(scale[axis] - 1.0f) < 1e-4f) {
            continue;
        }
        result[axis] = (std::max)(initial[axis] + (scale[axis] - 1.0f), minimumSize);
        if (snapEnabled && snapStep > 1e-6f) {
            result[axis] = (std::max)(
                std::round(result[axis] / snapStep) * snapStep,
                minimumSize);
        }
    }
    return Vector3(result[0], result[1], result[2]);
}

float snapScaleFactor(float factor, bool snapEnabled, float snapStep) {
    if (!snapEnabled || snapStep <= 1e-6f) return factor;
    return 1.0f + std::round((factor - 1.0f) / snapStep) * snapStep;
}

Vector3 effectiveGroupScaleFactors(const std::vector<Vector3>& initialSizes,
                                   const Vector3& rawFactors,
                                   bool snapEnabled, float snapStep,
                                   float minimumSize) {
    Vector3 result(
        snapScaleFactor(rawFactors.x, snapEnabled, snapStep),
        snapScaleFactor(rawFactors.y, snapEnabled, snapStep),
        snapScaleFactor(rawFactors.z, snapEnabled, snapStep));
    // One common lower bound per axis keeps every selected Cube above the
    // minimum while preserving a uniform handle's common scalar factor.
    Vector3 lower(0.0f, 0.0f, 0.0f);
    for (const Vector3& size : initialSizes) {
        if (size.x > 1e-6f) lower.x = (std::max)(lower.x, minimumSize / size.x);
        if (size.y > 1e-6f) lower.y = (std::max)(lower.y, minimumSize / size.y);
        if (size.z > 1e-6f) lower.z = (std::max)(lower.z, minimumSize / size.z);
    }
    const bool uniform = std::abs(rawFactors.x - rawFactors.y) < 1e-5f &&
                         std::abs(rawFactors.x - rawFactors.z) < 1e-5f;
    if (uniform) {
        const float commonLower = (std::max)({lower.x, lower.y, lower.z});
        const float common = (std::max)(result.x, commonLower);
        result = Vector3(common, common, common);
        return result;
    }
    result.x = (std::max)(result.x, lower.x);
    result.y = (std::max)(result.y, lower.y);
    result.z = (std::max)(result.z, lower.z);
    return result;
}

Vector3 groupScaleSize(const Vector3& initialSize, const Vector3& factors,
                       bool snapEnabled, float snapStep, float minimumSize) {
    const float values[3] = {initialSize.x, initialSize.y, initialSize.z};
    const float scale[3] = {factors.x, factors.y, factors.z};
    float result[3] = {};
    for (int axis = 0; axis < 3; ++axis) {
        const float f = snapScaleFactor(scale[axis], snapEnabled, snapStep);
        result[axis] = (std::max)(minimumSize, values[axis] * f);
    }
    return Vector3(result[0], result[1], result[2]);
}

Vector3 groupScalePosition(const Vector3& initialWorldPosition,
                           const Vector3& pivot,
                           const Vector3& effectiveFactors) {
    return pivot + Vector3(
        (initialWorldPosition.x - pivot.x) * effectiveFactors.x,
        (initialWorldPosition.y - pivot.y) * effectiveFactors.y,
        (initialWorldPosition.z - pivot.z) * effectiveFactors.z);
}

Vector3 fixedFaceResizeOrigin(
    const Vector3& initialOrigin,
    const Quaternion& worldRotation,
    const Vector3& initialSize,
    const Vector3& newSize,
    const Vector3& grabSigns,
    const Vector3& localCenterFactor) {
    const Vector3 centerBefore = initialOrigin + worldRotation.rotate(Vector3(
        initialSize.x * localCenterFactor.x,
        initialSize.y * localCenterFactor.y,
        initialSize.z * localCenterFactor.z));
    const Vector3 deltaSize = newSize - initialSize;
    const Vector3 centerAfter = centerBefore + worldRotation.rotate(Vector3(
        deltaSize.x * grabSigns.x,
        deltaSize.y * grabSigns.y,
        deltaSize.z * grabSigns.z)) * 0.5f;
    return centerAfter - worldRotation.rotate(Vector3(
        newSize.x * localCenterFactor.x,
        newSize.y * localCenterFactor.y,
        newSize.z * localCenterFactor.z));
}

} // namespace ViewportGeometry
