#include "include/Editor/GuiSnap.hpp"

#include <cmath>

namespace GuiSnap {

namespace {

// 始点・中央・終点の3本の線
struct Lines {
    float v[3];
};

Lines linesOf(float start, float length) {
    return {{start, start + length * 0.5f, start + length}};
}

void snapAxis(AxisSnap& out, const Lines& moving, const Lines& target, float& bestDistance) {
    for (float m : moving.v) {
        for (float t : target.v) {
            const float distance = std::fabs(t - m);
            if (distance > bestDistance) continue;
            bestDistance = distance;
            out.snapped = true;
            out.delta = t - m;
            out.guide = t;
        }
    }
}

} // namespace

Result snapRect(const ScreenGuiLayout::Rect& moving,
                const std::vector<ScreenGuiLayout::Rect>& targets,
                float threshold) {
    Result result;
    if (threshold <= 0.0f) return result;
    const Lines movingX = linesOf(moving.x, moving.w);
    const Lines movingY = linesOf(moving.y, moving.h);
    float bestX = threshold;
    float bestY = threshold;
    for (const ScreenGuiLayout::Rect& target : targets) {
        snapAxis(result.x, movingX, linesOf(target.x, target.w), bestX);
        snapAxis(result.y, movingY, linesOf(target.y, target.h), bestY);
    }
    return result;
}

AxisSnap snapLine(float line, const std::vector<ScreenGuiLayout::Rect>& targets,
                  bool horizontal, float threshold) {
    AxisSnap result;
    if (threshold <= 0.0f) return result;
    const Lines moving{{line, line, line}};
    float best = threshold;
    for (const ScreenGuiLayout::Rect& target : targets) {
        snapAxis(result, moving,
                 horizontal ? linesOf(target.x, target.w) : linesOf(target.y, target.h), best);
    }
    return result;
}

const std::vector<ResizeHandle>& resizeHandles() {
    static const std::vector<ResizeHandle> handles = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1, 0},           {1, 0},
        {-1, 1},  {0, 1},  {1, 1},
    };
    return handles;
}

float handleCenterX(const ScreenGuiLayout::Rect& rect, const ResizeHandle& handle) {
    return rect.x + rect.w * 0.5f * static_cast<float>(handle.x + 1);
}

float handleCenterY(const ScreenGuiLayout::Rect& rect, const ResizeHandle& handle) {
    return rect.y + rect.h * 0.5f * static_cast<float>(handle.y + 1);
}

namespace {

// 1軸ぶんの辺 [low, high] のうち、side 側(-1=low, +1=high)の辺を delta 動かして吸着・最小幅を適用する
void resizeAxis(float& low, float& high, int side, float delta,
                const std::vector<ScreenGuiLayout::Rect>& targets, bool horizontal,
                float threshold, float minSize, AxisSnap& snapOut) {
    if (side == 0) return;
    float& edge = side < 0 ? low : high;
    edge += delta;
    snapOut = snapLine(edge, targets, horizontal, threshold);
    edge += snapOut.delta;
    if (high - low < minSize) {
        // 反対側の辺は固定したまま最小幅に戻す（吸着は無効になる）
        edge = side < 0 ? high - minSize : low + minSize;
        snapOut = {};
    }
}

} // namespace

ScreenGuiLayout::Rect resizeRect(const ScreenGuiLayout::Rect& start, const ResizeHandle& handle,
                                 float dx, float dy,
                                 const std::vector<ScreenGuiLayout::Rect>& targets,
                                 float threshold, float minSize, Result& snapOut) {
    snapOut = {};
    float left = start.x;
    float right = start.x + start.w;
    float top = start.y;
    float bottom = start.y + start.h;
    resizeAxis(left, right, handle.x, dx, targets, true, threshold, minSize, snapOut.x);
    resizeAxis(top, bottom, handle.y, dy, targets, false, threshold, minSize, snapOut.y);
    return {left, top, right - left, bottom - top};
}

} // namespace GuiSnap
