#include "Instances/Camera.hpp"
#include "Instances/Workspace.hpp"
#include "Core/PropertyRegistry.hpp"
#include <cmath>

namespace {
const bool s_cameraRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("Camera", "Spatial", {
        field<&Camera::rotationSpeed>("RotationSpeed", 0.0f, 10.0f, 0.01f),
        field<&Camera::mouseRotationSpeed>("MouseRotationSpeed", 0.0f, 2.0f, 0.01f),
        field<&Camera::cameraDistance>("CameraDistance", 1.0f, 50.0f, 0.1f),
        field<&Camera::zoomSpeed>("ZoomSpeed", 0.0f, 1.0f, 0.01f),
        field<&Camera::mouseZoomSpeed>("MouseZoomSpeed", 0.0f, 10.0f, 0.1f),
        field<&Camera::firstPersonThreshold>("FirstPersonThreshold", 0.0f, 50.0f, 0.1f),
        field<&Camera::minCameraDistance>("MinCameraDistance", 0.0f, 50.0f, 0.1f),
        field<&Camera::ctrlLockOffsetDistance>("CtrlLockOffsetDistance", 0.0f, 50.0f, 0.1f),
        instanceRefField<&Camera::WorkspacePath>("Workspace", "Workspace")
    });
    return true;
}();
}

Camera::Camera()
    : Spatial(Vector3(0.0f, -2.0f, 5.0f), Vector3(1.0f, 1.0f, 1.0f), "Camera") {
    WorkspacePath.clear();
}

bool Camera::IsA(std::string className) {
    return className == "Camera" || Spatial::IsA(className);
}

void Camera::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Workspace") setRuntimeWorkspace(nullptr);
    if (PropertyRegistry::loadProperty(this, "Camera", name, value)) return;
    Spatial::setProperty(name, value);
}

std::shared_ptr<Instance> Camera::clone() const {
    auto result = std::make_shared<Camera>();
    result->Name = Name;
    result->setCFrame(getCFrame());
    result->Size = Size;
    result->rotationSpeed = rotationSpeed;
    result->mouseRotationSpeed = mouseRotationSpeed;
    result->cameraDistance = cameraDistance;
    result->zoomSpeed = zoomSpeed;
    result->mouseZoomSpeed = mouseZoomSpeed;
    result->firstPersonThreshold = firstPersonThreshold;
    result->minCameraDistance = minCameraDistance;
    result->ctrlLockOffsetDistance = ctrlLockOffsetDistance;
    result->WorkspacePath = WorkspacePath;
    return result;
}

CFrame Camera::getWorldCFrame() const {
    return getCFrame();
}

void Camera::setCFrame(const CFrame& value) {
    // Spatial::setCFrame only changes this node's local frame. Since the
    // world frame is intentionally parent-independent, that is the desired
    // operation for Camera as well.
    Spatial::commitCFrame(value, SpatialUpdateOrigin::Script);
}

Workspace* Camera::getReferencedWorkspace() const {
    if (auto runtime = m_runtimeWorkspace.lock()) return runtime.get();
    if (WorkspacePath.empty()) return nullptr;
    Instance* root = const_cast<Camera*>(this);
    while (auto parent = root->Parent.lock()) root = parent.get();
    Instance* found = root->getChildByPath(WorkspacePath);
    if (!found || !found->IsA("Workspace")) {
        return nullptr;
    }
    return static_cast<Workspace*>(found);
}

void Camera::setRuntimeWorkspace(Workspace* workspace) {
    if (!workspace) {
        m_runtimeWorkspace.reset();
        WorkspacePath.clear();
        return;
    }
    try {
        m_runtimeWorkspace = std::static_pointer_cast<Workspace>(workspace->shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        RCBN_WARN("Camera '" << getFullPath() << "' cannot retain runtime Workspace without ownership");
        m_runtimeWorkspace.reset();
    }
}
