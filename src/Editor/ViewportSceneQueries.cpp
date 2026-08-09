#include <Editor/ViewportSceneQueries.hpp>

#include <Instances/BaseCube.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Model.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Workspace.hpp>

#include <cmath>

namespace ViewportSceneQueries {
namespace {

void findNearestBaseCubeRecursive(
    Instance* instance,
    const ViewportGeometry::Ray& ray,
    Instance* exclude,
    float& nearestDistance,
    BaseCubeRayHit& nearest) {
    if (!instance || instance == exclude) {
        return;
    }
    if (instance->getClassName() == "Skybox") {
        return;
    }

    if (instance->IsA("BaseCube")) {
        auto* cube = static_cast<BaseCube*>(instance);
        const ViewportGeometry::ObbRayHit hit = ViewportGeometry::raycastObb(
            ray,
            cube->getWorldCFrame(),
            cube->Size);
        if (hit.hit && hit.distance < nearestDistance) {
            nearestDistance = hit.distance;
            nearest.hit = true;
            nearest.cube = cube;
            nearest.obb = hit;
        }
    }

    for (auto const& [_, child] : instance->getChildren()) {
        findNearestBaseCubeRecursive(child.get(), ray, exclude, nearestDistance, nearest);
    }
}

void findPickerTargetRecursive(
    Instance* instance,
    const ViewportGeometry::Ray& ray,
    PickerTargetType targetType,
    float& nearestDistance,
    PickerRayHit& nearest) {
    if (!instance) {
        return;
    }

    const bool targetMatches = targetType == PickerTargetType::Attachment
        ? instance->IsA("Attachment")
        : instance->IsA("BaseCube");
    if (targetMatches) {
        auto* spatial = static_cast<Spatial*>(instance);
        const Vector3 hitSize = targetType == PickerTargetType::Attachment
            ? Vector3(0.5f, 0.5f, 0.5f)
            : spatial->Size;
        const ViewportGeometry::ObbRayHit hit = ViewportGeometry::raycastObb(
            ray,
            spatial->getWorldCFrame(),
            hitSize);
        if (hit.hit && hit.distance < nearestDistance) {
            nearestDistance = hit.distance;
            nearest.hit = true;
            nearest.target = instance;
            nearest.distance = hit.distance;
        }
    }

    for (auto const& [_, child] : instance->getChildren()) {
        findPickerTargetRecursive(child.get(), ray, targetType, nearestDistance, nearest);
    }
}

void collectBoxSelectableCubesRecursive(
    Instance* instance,
    const Matrix4& viewProjection,
    const Vector2& viewportOrigin,
    const Vector2& viewportSize,
    const Vector2& selectionMinimum,
    const Vector2& selectionMaximum,
    std::vector<Instance*>& result) {
    if (!instance) {
        return;
    }
    if (instance->getClassName() == "Skybox") {
        return;
    }

    if (instance->IsA("BaseCube")) {
        auto* cube = static_cast<BaseCube*>(instance);
        if (cube->Locked) {
            return;
        }

        const ViewportGeometry::ProjectedPoint projected =
            ViewportGeometry::projectWorldToScreen(
                viewProjection,
                cube->getWorldPosition(),
                viewportOrigin,
                viewportSize);
        if (projected.visible
            && projected.position.x >= selectionMinimum.x
            && projected.position.x <= selectionMaximum.x
            && projected.position.y >= selectionMinimum.y
            && projected.position.y <= selectionMaximum.y) {
            result.push_back(instance);
        }
    }

    for (auto const& [_, child] : instance->getChildren()) {
        collectBoxSelectableCubesRecursive(
            child.get(),
            viewProjection,
            viewportOrigin,
            viewportSize,
            selectionMinimum,
            selectionMaximum,
            result);
    }
}

void accumulateDescendantWorldAabbRecursive(
    Instance* instance,
    ViewportGeometry::WorldAabb& aabb) {
    if (!instance) {
        return;
    }
    if (instance->IsA("BaseCube")) {
        auto* spatial = static_cast<Spatial*>(instance);
        ViewportGeometry::accumulateWorldAabb(aabb, spatial->getWorldCFrame(), spatial->Size);
    }
    for (auto const& [_, child] : instance->getChildren()) {
        accumulateDescendantWorldAabbRecursive(child.get(), aabb);
    }
}

void collectHighlightBaseCubesRecursive(
    Instance* instance,
    std::vector<BaseCube*>& result) {
    if (!instance) {
        return;
    }
    if (instance->IsA("BaseCube")) {
        result.push_back(static_cast<BaseCube*>(instance));
    }
    for (auto const& [_, child] : instance->getChildren()) {
        collectHighlightBaseCubesRecursive(child.get(), result);
    }
}

void fitBoundsOnAxisRecursive(
    Instance* instance,
    Instance* movingRoot,
    const MovementBounds& movingBounds,
    const Vector3& axisDirection,
    int axis,
    float position[3]) {
    if (!instance || instance == movingRoot) {
        return;
    }
    if (instance->getClassName() == "Skybox") {
        return;
    }

    if (instance->IsA("BaseCube")) {
        auto* other = static_cast<BaseCube*>(instance);
        const Vector3 otherWorldPosition = other->getWorldPosition();
        const Quaternion otherRotation = other->getWorldCFrame().Rotation;
        const float otherPosition[3] = {
            otherWorldPosition.x,
            otherWorldPosition.y,
            otherWorldPosition.z
        };
        if (ViewportGeometry::obbIntersects(
                Vector3(position[0], position[1], position[2]),
                movingBounds.rotation,
                movingBounds.size,
                otherWorldPosition,
                otherRotation,
                other->Size)) {
            const float overlap = ViewportGeometry::obbSupportRadius(
                    movingBounds.rotation, movingBounds.size, axisDirection)
                + ViewportGeometry::obbSupportRadius(
                    otherRotation, other->Size, axisDirection)
                - std::abs(position[axis] - otherPosition[axis]);
            if (overlap > 0.0f) {
                const float difference = position[axis] - otherPosition[axis];
                position[axis] += difference >= 0.0f ? overlap : -overlap;
            }
        }
    }

    for (auto const& [_, child] : instance->getChildren()) {
        fitBoundsOnAxisRecursive(
            child.get(), movingRoot, movingBounds, axisDirection, axis, position);
    }
}

void fitBoundsCollisionRecursive(
    Instance* instance,
    Instance* movingRoot,
    const MovementBounds& movingBounds,
    Vector3& position) {
    if (!instance || instance == movingRoot) {
        return;
    }
    if (instance->getClassName() == "Skybox") {
        return;
    }

    if (instance->IsA("BaseCube")) {
        auto* other = static_cast<BaseCube*>(instance);
        const Vector3 otherWorldPosition = other->getWorldPosition();
        const Quaternion otherRotation = other->getWorldCFrame().Rotation;
        if (ViewportGeometry::obbIntersects(
                position,
                movingBounds.rotation,
                movingBounds.size,
                otherWorldPosition,
                otherRotation,
                other->Size)) {
            const float overlapX = ViewportGeometry::obbSupportRadius(
                    movingBounds.rotation, movingBounds.size, Vector3(1.0f, 0.0f, 0.0f))
                + ViewportGeometry::obbSupportRadius(
                    otherRotation, other->Size, Vector3(1.0f, 0.0f, 0.0f))
                - std::abs(position.x - otherWorldPosition.x);
            const float overlapY = ViewportGeometry::obbSupportRadius(
                    movingBounds.rotation, movingBounds.size, Vector3(0.0f, 1.0f, 0.0f))
                + ViewportGeometry::obbSupportRadius(
                    otherRotation, other->Size, Vector3(0.0f, 1.0f, 0.0f))
                - std::abs(position.y - otherWorldPosition.y);
            const float overlapZ = ViewportGeometry::obbSupportRadius(
                    movingBounds.rotation, movingBounds.size, Vector3(0.0f, 0.0f, 1.0f))
                + ViewportGeometry::obbSupportRadius(
                    otherRotation, other->Size, Vector3(0.0f, 0.0f, 1.0f))
                - std::abs(position.z - otherWorldPosition.z);
            if (overlapX > 0.0f && overlapY > 0.0f && overlapZ > 0.0f) {
                const float differenceX = position.x - otherWorldPosition.x;
                const float differenceY = position.y - otherWorldPosition.y;
                const float differenceZ = position.z - otherWorldPosition.z;
                if (overlapX <= overlapY && overlapX <= overlapZ) {
                    position.x += differenceX >= 0.0f ? overlapX : -overlapX;
                } else if (overlapY <= overlapX && overlapY <= overlapZ) {
                    position.y += differenceY >= 0.0f ? overlapY : -overlapY;
                } else {
                    position.z += differenceZ >= 0.0f ? overlapZ : -overlapZ;
                }
            }
        }
    }

    for (auto const& [_, child] : instance->getChildren()) {
        fitBoundsCollisionRecursive(child.get(), movingRoot, movingBounds, position);
    }
}

} // namespace

bool isLockedBaseCube(Instance* instance) {
    return instance && instance->IsA("BaseCube")
        && static_cast<BaseCube*>(instance)->Locked;
}

BaseCubeRayHit findNearestBaseCube(
    Workspace& workspace,
    const ViewportGeometry::Ray& ray,
    Instance* exclude) {
    BaseCubeRayHit result;
    float nearestDistance = 1e30f;
    findNearestBaseCubeRecursive(&workspace, ray, exclude, nearestDistance, result);
    return result;
}

SelectionRayHit findSelectionTarget(
    Workspace& workspace,
    const ViewportGeometry::Ray& ray) {
    const BaseCubeRayHit cubeHit = findNearestBaseCube(workspace, ray);
    SelectionRayHit result;
    if (!cubeHit.hit || !cubeHit.cube) {
        return result;
    }

    result.hit = true;
    result.cube = cubeHit.cube;
    result.obb = cubeHit.obb;
    result.locked = cubeHit.cube->Locked;
    result.target = cubeHit.cube;
    if (result.locked) {
        return result;
    }

    Instance* topmostModel = nullptr;
    auto parent = cubeHit.cube->Parent.lock();
    while (parent && !parent->IsA("Workspace")) {
        if (parent->IsA("Model")) {
            topmostModel = parent.get();
        }
        parent = parent->Parent.lock();
    }
    if (topmostModel) {
        result.target = topmostModel;
    }
    return result;
}

PickerRayHit findPickerTarget(
    Workspace& workspace,
    const ViewportGeometry::Ray& ray,
    PickerTargetType targetType) {
    PickerRayHit result;
    float nearestDistance = 1e30f;
    findPickerTargetRecursive(&workspace, ray, targetType, nearestDistance, result);
    return result;
}

std::vector<Instance*> collectBoxSelectableCubes(
    Workspace& workspace,
    const Matrix4& viewProjection,
    const Vector2& viewportOrigin,
    const Vector2& viewportSize,
    const Vector2& selectionMinimum,
    const Vector2& selectionMaximum) {
    std::vector<Instance*> result;
    collectBoxSelectableCubesRecursive(
        &workspace,
        viewProjection,
        viewportOrigin,
        viewportSize,
        selectionMinimum,
        selectionMaximum,
        result);
    return result;
}

ViewportGeometry::WorldAabb computeDescendantWorldAabb(Instance& root) {
    ViewportGeometry::WorldAabb result;
    for (auto const& [_, child] : root.getChildren()) {
        accumulateDescendantWorldAabbRecursive(child.get(), result);
    }
    return result;
}

MovementBounds computeMovementBounds(Instance& target) {
    MovementBounds result;
    if (target.IsA("Model")) {
        const ViewportGeometry::WorldAabb aabb = computeDescendantWorldAabb(target);
        if (!aabb.valid) {
            return result;
        }
        result.valid = true;
        result.center = (aabb.minimum + aabb.maximum) * 0.5f;
        result.size = aabb.maximum - aabb.minimum;
        result.rotation = Quaternion();
        return result;
    }
    if (target.IsA("Spatial")) {
        auto* spatial = static_cast<Spatial*>(&target);
        result.valid = true;
        result.center = spatial->getWorldPosition();
        result.size = spatial->Size;
        result.rotation = spatial->getWorldCFrame().Rotation;
    }
    return result;
}

std::vector<BaseCube*> collectHighlightBaseCubes(Instance& target) {
    std::vector<BaseCube*> result;
    if (target.IsA("BaseCube")) {
        result.push_back(static_cast<BaseCube*>(&target));
    } else {
        for (auto const& [_, child] : target.getChildren()) {
            collectHighlightBaseCubesRecursive(child.get(), result);
        }
    }
    return result;
}

float fitOnAxis(
    Workspace& workspace,
    Vector3 center,
    const MovementBounds& bounds,
    Instance& movingRoot,
    int axis) {
    if (!bounds.valid || axis < 0 || axis > 2) {
        return axis == 0 ? center.x : axis == 1 ? center.y : center.z;
    }
    const Vector3 axisDirection = axis == 0
        ? Vector3(1.0f, 0.0f, 0.0f)
        : axis == 1
            ? Vector3(0.0f, 1.0f, 0.0f)
            : Vector3(0.0f, 0.0f, 1.0f);
    float position[3] = { center.x, center.y, center.z };
    fitBoundsOnAxisRecursive(
        &workspace, &movingRoot, bounds, axisDirection, axis, position);
    return position[axis];
}

Vector3 fitCollision(
    Workspace& workspace,
    Vector3 center,
    const MovementBounds& bounds,
    Instance& movingRoot) {
    if (!bounds.valid) {
        return center;
    }
    fitBoundsCollisionRecursive(&workspace, &movingRoot, bounds, center);
    return center;
}

} // namespace ViewportSceneQueries
