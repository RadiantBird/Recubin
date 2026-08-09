#pragma once

#include <Math/CFrame.hpp>
#include <Math/Matrix4.hpp>
#include <Math/Quaternion.hpp>
#include <Math/Vector2.hpp>
#include <Math/Vector3.hpp>

class Spatial;

namespace ViewportGeometry {

struct Ray {
    Vector3 origin;
    Vector3 direction;
};

struct ObbRayHit {
    bool hit = false;
    float distance = -1.0f;
    int axis = 1;
    float sign = 1.0f;
};

struct ProjectedPoint {
    bool visible = false;
    Vector2 position;
};

struct WorldAabb {
    bool valid = false;
    Vector3 minimum;
    Vector3 maximum;
};

Vector3 worldToLocalPosition(const Vector3& worldPosition, const Spatial& spatial);
Quaternion worldToLocalRotation(const Quaternion& worldRotation, const Spatial& spatial);

Ray makeScreenRay(
    const Vector3& cameraPosition,
    const Vector3& cameraForward,
    const Vector3& cameraRight,
    const Vector3& cameraUp,
    const Vector2& screenPosition,
    const Vector2& viewportSize);

ObbRayHit raycastObb(const Ray& ray, const CFrame& worldCFrame, const Vector3& size);

float obbSupportRadius(
    const Quaternion& rotation,
    const Vector3& size,
    const Vector3& direction);

bool obbIntersects(
    const Vector3& positionA,
    const Quaternion& rotationA,
    const Vector3& sizeA,
    const Vector3& positionB,
    const Quaternion& rotationB,
    const Vector3& sizeB);

ProjectedPoint projectWorldToScreen(
    const Matrix4& viewProjection,
    const Vector3& worldPosition,
    const Vector2& viewportOrigin,
    const Vector2& viewportSize);

void accumulateWorldAabb(
    WorldAabb& aabb,
    const CFrame& worldCFrame,
    const Vector3& size);

Vector3 additiveResize(
    const Vector3& initialSize,
    const Vector3& gizmoScale,
    bool snapEnabled,
    float snapStep,
    float minimumSize = 0.05f);

} // namespace ViewportGeometry
