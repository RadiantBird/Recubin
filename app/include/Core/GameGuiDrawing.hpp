#pragma once

#include <filesystem>
#include <functional>

#include <include/imgui/imgui.h>
#include <include/Math/Vector3.hpp>

class Workspace;
class User;
class GuiButton;
class ScreenGuiObject;
struct GameGuiRenderContext;

namespace GameGuiDrawing {

// Resources supplied by a renderer-specific front end. The legacy renderer
// still owns the image cache, while the SDL path can provide its own handles
// as the texture bridge is migrated.
struct Resources {
    std::function<ImFont*(ScreenGuiObject*)> font;
    std::function<ImTextureRef(ScreenGuiObject*)> image;
    bool flipImageY = false;
};

std::filesystem::path customFontPath(ScreenGuiObject& object);
ImFont* addCustomFont(const std::filesystem::path& path);

GameGuiRenderContext makeContext(
    float viewportX, float viewportY, float viewportWidth, float viewportHeight,
    const Vector3& cameraPosition, const Vector3& cameraForward,
    const Vector3& cameraRight, const Vector3& cameraUp,
    float projectionAspect, bool recordUserViewport = true);

void draw(Workspace& workspace, User* user, const GameGuiRenderContext& context,
          const Resources& resources,
          const std::function<void(GuiButton*)>& onButtonActivated,
          bool inputEnabled = true);

void drawContent(ImDrawList* drawList, ScreenGuiObject* element,
                 float x, float y, float width, float height,
                 const Resources& resources = {});

} // namespace GameGuiDrawing
