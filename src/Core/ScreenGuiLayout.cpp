#include "include/Core/ScreenGuiLayout.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/System.hpp"
#include "include/Instances/Workspace.hpp"

#include <algorithm>
#include <cmath>

namespace ScreenGuiLayout {

Vector2 pixelScale(const Workspace& workspace, float viewportWidth, float viewportHeight,
                   bool uniform) {
    Vector2 scale(1.0f, 1.0f);
    if (auto parent = workspace.Parent.lock(); parent && parent->IsA("System")) {
        auto* system = static_cast<System*>(parent.get());
        if (system->BaseResolution.x > 0.f) scale.x = viewportWidth / system->BaseResolution.x;
        if (system->BaseResolution.y > 0.f) scale.y = viewportHeight / system->BaseResolution.y;
    }
    if (uniform) {
        const float common = (std::min)(scale.x, scale.y);
        scale = Vector2(common, common);
    }
    return scale;
}

Rect computeRect(const ScreenGuiObject& object,
                 float viewportX, float viewportY,
                 float viewportWidth, float viewportHeight,
                 const Vector2& scale) {
    const bool normalized = object.NormType == Norm::Scale;
    Rect rect;
    rect.x = (normalized ? object.Position.x * viewportWidth : object.Position.x * scale.x) + viewportX;
    rect.y = (normalized ? object.Position.y * viewportHeight : object.Position.y * scale.y) + viewportY;
    rect.w = normalized ? object.Size.x * viewportWidth : object.Size.x * scale.x;
    rect.h = normalized ? object.Size.y * viewportHeight : object.Size.y * scale.y;
    return rect;
}

Rect letterbox(const Rect& area, const Vector2& aspect) {
    if (area.w <= 0.0f || area.h <= 0.0f || aspect.x <= 0.0f || aspect.y <= 0.0f) return area;
    const float scale = (std::min)(area.w / aspect.x, area.h / aspect.y);
    Rect result;
    result.w = (std::max)(1.0f, std::floor(aspect.x * scale));
    result.h = (std::max)(1.0f, std::floor(aspect.y * scale));
    result.x = area.x + std::floor((area.w - result.w) * 0.5f);
    result.y = area.y + std::floor((area.h - result.h) * 0.5f);
    return result;
}

bool lockedAspectArea(const Workspace& workspace, const Rect& area, Rect& out) {
    auto parent = workspace.Parent.lock();
    if (!parent || !parent->IsA("System")) return false;
    const auto* system = static_cast<const System*>(parent.get());
    if (!system->LockAspectRatio) return false;
    out = letterbox(area, system->BaseResolution);
    return true;
}

bool hasWorldGuiAncestor(const ScreenGuiObject& object) {
    auto parent = object.Parent.lock();
    while (parent) {
        if (parent->IsA("WorldGuiObject")) return true;
        parent = parent->Parent.lock();
    }
    return false;
}

std::vector<ScreenGuiObject*> collectScreenElements(const Workspace& workspace) {
    std::vector<ScreenGuiObject*> elements;
    for (ScreenGuiObject* object : workspace.getRenderScreenGuiObjects()) {
        if (object && !hasWorldGuiAncestor(*object)) elements.push_back(object);
    }
    std::sort(elements.begin(), elements.end(), [](ScreenGuiObject* a, ScreenGuiObject* b) {
        return a->ZIndex < b->ZIndex;
    });
    return elements;
}

} // namespace ScreenGuiLayout
