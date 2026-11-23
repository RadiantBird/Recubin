#pragma once

#include <include/imgui/imgui.h>

class ViewportPanel;
struct ViewportRenderDesc;

// Host-owned viewport renderer.  No SDL/GPU types cross this editor boundary.
class ViewportSceneRenderer {
public:
    virtual ~ViewportSceneRenderer() = default;
    virtual ImTextureRef renderViewport(const ViewportPanel& pane,
                                        const ViewportRenderDesc& request) = 0;
    virtual void removeViewport(const ViewportPanel& pane) = 0;
};
