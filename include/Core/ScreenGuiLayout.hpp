#pragma once
#include <Math/Vector2.hpp>
#include <vector>

class Workspace;
class ScreenGuiObject;

// 画面GUI(ScreenGuiObject)の画面上の矩形計算。
// ランタイム描画(Renderer_GUI)とエディターのビューポート編集で同じ計算を共有する。
namespace ScreenGuiLayout {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Norm::Pixel 要素のスケール係数: System.BaseResolution に対する現在のビューポート比率。
// System が見つからない/BaseResolutionが不正な場合は 1.0（ピクセル等倍）にフォールバックする。
// uniform=true なら縦横の比率の小さい方を両軸に使い、縦横比が違うビューポートでも歪ませない。
Vector2 pixelScale(const Workspace& workspace, float viewportWidth, float viewportHeight,
                   bool uniform = false);

// 要素の画面上の矩形（ビューポート原点 viewportX/Y を含む絶対座標）
Rect computeRect(const ScreenGuiObject& object,
                 float viewportX, float viewportY,
                 float viewportWidth, float viewportHeight,
                 const Vector2& scale);

// WorldGui(SurfaceGui/BillboardGui)配下の要素か（画面GUIとしては描画しない）
bool hasWorldGuiAncestor(const ScreenGuiObject& object);

// 画面GUIとして描画される要素を ZIndex 昇順（小 = 背面）で返す
std::vector<ScreenGuiObject*> collectScreenElements(const Workspace& workspace);

} // namespace ScreenGuiLayout
