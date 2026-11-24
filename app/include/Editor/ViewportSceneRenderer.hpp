#pragma once

#include <include/imgui/imgui.h>
#include <span>

class ViewportPanel;
struct ViewportRenderDesc;
class BaseCube;
class User;
class Workspace;
struct GameGuiRenderContext;
struct Color4;
struct Vector3;

// Host-owned viewport renderer.  No SDL/GPU types cross this editor boundary.
class ViewportSceneRenderer {
public:
    virtual ~ViewportSceneRenderer() = default;
    virtual ImTextureRef renderViewport(const ViewportPanel& pane,
                                        const ViewportRenderDesc& request) = 0;
    virtual void renderGameGui(Workspace& workspace, User* user,
                               const GameGuiRenderContext& context, bool inputEnabled) = 0;
    virtual void renderOutline(const ViewportPanel& pane,
                               std::span<BaseCube* const> targets,
                               const Color4& color, float thickness) = 0;
    virtual void renderTerrainGuide(const ViewportPanel& pane,
                                    const Vector3& center, const Vector3& normal,
                                    float radius) = 0;
    virtual void removeViewport(const ViewportPanel& pane) = 0;
};
