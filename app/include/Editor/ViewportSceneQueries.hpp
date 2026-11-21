#pragma once

#include <Editor/ViewportGeometry.hpp>

#include <vector>

class BaseCube;
class Instance;
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

// 通常選択／ホバー用。レイ上の最前面 Cube を保持しつつ、選択単位だけを
// 最上位 Model へ昇格する。Locked は背後を遮るため target へ昇格しない。
struct SelectionRayHit {
    bool hit = false;
    bool locked = false;
    BaseCube* cube = nullptr;
    Instance* target = nullptr;
    ViewportGeometry::ObbRayHit obb;
};

// 表面ドラッグと衝突フィットが共有するワールド境界。
// BaseCube は OBB、Model は子孫 BaseCube のワールド AABB（rotation=identity）。
struct MovementBounds {
    bool valid = false;
    Vector3 center;
    Vector3 size;
    Quaternion rotation;
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

SelectionRayHit findSelectionTarget(
    Workspace& workspace,
    const ViewportGeometry::Ray& ray);

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

MovementBounds computeMovementBounds(Instance& target);

std::vector<BaseCube*> collectHighlightBaseCubes(Instance& target);

float fitOnAxis(
    Workspace& workspace,
    Vector3 center,
    const MovementBounds& bounds,
    Instance& movingRoot,
    int axis);

Vector3 fitCollision(
    Workspace& workspace,
    Vector3 center,
    const MovementBounds& bounds,
    Instance& movingRoot);

} // namespace ViewportSceneQueries
