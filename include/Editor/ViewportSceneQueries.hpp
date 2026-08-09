#pragma once

#include <Editor/ViewportGeometry.hpp>

#include <vector>

class BaseCube;
class Instance;
class Spatial;
class Workspace;

namespace ViewportSceneQueries {

enum class PickerTargetType {
    BaseCube,
    Attachment
};

struct BaseCubeRayHit {
    bool hit = false;
    BaseCube* cube = nullptr;
    ViewportGeometry::ObbRayHit obb;
};

struct PickerRayHit {
    bool hit = false;
    Instance* target = nullptr;
    float distance = -1.0f;
};

bool isLockedBaseCube(Instance* instance);

BaseCubeRayHit findNearestBaseCube(
    Workspace& workspace,
    const ViewportGeometry::Ray& ray,
    Instance* exclude = nullptr);

PickerRayHit findPickerTarget(
    Workspace& workspace,
    const ViewportGeometry::Ray& ray,
    PickerTargetType targetType);

std::vector<Instance*> collectBoxSelectableCubes(
    Workspace& workspace,
    const Matrix4& viewProjection,
    const Vector2& viewportOrigin,
    const Vector2& viewportSize,
    const Vector2& selectionMinimum,
    const Vector2& selectionMaximum);

ViewportGeometry::WorldAabb computeDescendantWorldAabb(Instance& root);

float fitOnAxis(
    Workspace& workspace,
    Vector3 position,
    const Vector3& size,
    Spatial& moving,
    int axis);

Vector3 fitCollision(
    Workspace& workspace,
    Vector3 position,
    const Vector3& size,
    Spatial& moving);

} // namespace ViewportSceneQueries
