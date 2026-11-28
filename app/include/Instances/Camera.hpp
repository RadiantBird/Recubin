#pragma once

#include "Spatial.hpp"
#include <memory>
#include <string>

class Workspace;

// A camera is a world-space observer.  Its transform is deliberately not
// inherited from Instance parents so cameras can observe another Workspace.
class Camera : public Spatial {
public:
    float rotationSpeed = 1.0f;
    float mouseRotationSpeed = 0.15f;
    float cameraDistance = 10.0f;
    float zoomSpeed = 0.1f;
    float mouseZoomSpeed = 1.0f;
    float firstPersonThreshold = 1.5f;
    float minCameraDistance = 0.5f;
    float ctrlLockOffsetDistance = 2.0f;

    // Workspace-relative path. Empty means that the reference is unset.
    std::string WorkspacePath;

    Camera();
    std::string getClassName() override { return "Camera"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
    CFrame getWorldCFrame() const override;
    void setCFrame(const CFrame& value) override;

    Workspace* getReferencedWorkspace() const;
    void setRuntimeWorkspace(Workspace* workspace);

private:
    std::weak_ptr<Workspace> m_runtimeWorkspace;
};
