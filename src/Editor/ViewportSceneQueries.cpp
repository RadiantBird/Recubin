#include <Editor/ViewportSceneQueries.hpp>

#include <Instances/BaseCube.hpp>
#include <Instances/Instance.hpp>
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

void fitOnAxisRecursive(
    Instance* instance,
    Instance* moving,
    const Quaternion& movingRotation,
    const Vector3& size,
    const Vector3& axisDirection,
    int axis,
    float position[3]) {
    if (!instance || instance == moving) {
        return;
    }
    if (instance->getClassName() == "Skybox") {
        return;
    }

    if (instance->IsA("Spatial")) {
        auto* other = static_cast<Spatial*>(instance);
        const Vector3 otherWorldPosition = other->getWorldPosition();
        const Quaternion otherRotation = other->getWorldCFrame().Rotation;
        const float otherPosition[3] = {
            otherWorldPosition.x,
            otherWorldPosition.y,
            otherWorldPosition.z
        };
        if (ViewportGeometry::obbIntersects(
                Vector3(position[0], position[1], position[2]),
                movingRotation,
                size,
                otherWorldPosition,
                otherRotation,
                other->Size)) {
            const float overlap = ViewportGeometry::obbSupportRadius(
                    movingRotation, size, axisDirection)
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
        fitOnAxisRecursive(
            child.get(),
            moving,
            movingRotation,
            size,
            axisDirection,
            axis,
            position);
    }
}

void fitCollisionRecursive(
    Instance* instance,
    Instance* moving,
    const Quaternion& movingRotation,
    const Vector3& size,
    Vector3& position) {
    if (!instance || instance == moving) {
        return;
    }
    if (instance->getClassName() == "Skybox") {
        return;
    }

    if (instance->IsA("Spatial")) {
        auto* other = static_cast<Spatial*>(instance);
        const Vector3 otherWorldPosition = other->getWorldPosition();
        const Quaternion otherRotation = other->getWorldCFrame().Rotation;
        if (ViewportGeometry::obbIntersects(
                position,
                movingRotation,
                size,
                otherWorldPosition,
                otherRotation,
                other->Size)) {
            const float overlapX = ViewportGeometry::obbSupportRadius(
                    movingRotation, size, Vector3(1.0f, 0.0f, 0.0f))
                + ViewportGeometry::obbSupportRadius(
                    otherRotation, other->Size, Vector3(1.0f, 0.0f, 0.0f))
                - std::abs(position.x - otherWorldPosition.x);
            const float overlapY = ViewportGeometry::obbSupportRadius(
                    movingRotation, size, Vector3(0.0f, 1.0f, 0.0f))
                + ViewportGeometry::obbSupportRadius(
                    otherRotation, other->Size, Vector3(0.0f, 1.0f, 0.0f))
                - std::abs(position.y - otherWorldPosition.y);
            const float overlapZ = ViewportGeometry::obbSupportRadius(
                    movingRotation, size, Vector3(0.0f, 0.0f, 1.0f))
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
        fitCollisionRecursive(
            child.get(),
            moving,
            movingRotation,
            size,
            position);
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

float fitOnAxis(
    Workspace& workspace,
    Vector3 position,
    const Vector3& size,
    Spatial& moving,
    int axis) {
    const Quaternion movingRotation = moving.getWorldCFrame().Rotation;
    const Vector3 axisDirection = axis == 0
        ? Vector3(1.0f, 0.0f, 0.0f)
        : axis == 1
            ? Vector3(0.0f, 1.0f, 0.0f)
            : Vector3(0.0f, 0.0f, 1.0f);
    float positionComponents[3] = { position.x, position.y, position.z };
    fitOnAxisRecursive(
        &workspace,
        &moving,
        movingRotation,
        size,
        axisDirection,
        axis,
        positionComponents);
    return positionComponents[axis];
}

Vector3 fitCollision(
    Workspace& workspace,
    Vector3 position,
    const Vector3& size,
    Spatial& moving) {
    const Quaternion movingRotation = moving.getWorldCFrame().Rotation;
    fitCollisionRecursive(
        &workspace,
        &moving,
        movingRotation,
        size,
        position);
    return position;
}

} // namespace ViewportSceneQueries
