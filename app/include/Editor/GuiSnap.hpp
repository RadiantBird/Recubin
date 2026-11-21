#pragma once
#include <Core/ScreenGuiLayout.hpp>
#include <vector>

// ビューポートでGUI要素をドラッグするときの吸着（スマートガイド）計算。
// 動かす矩形の 左/中央/右・上/中央/下 を、他の矩形の同じ線へ閾値内なら吸着させる。
// 左辺⇔右辺の吸着でぴったり隣接し、そのまま滑らせると中央線で揃う。
// 壁のように押し返す衝突はしない（磁石のような吸着のみ）。
namespace GuiSnap {

struct AxisSnap {
    bool  snapped = false;
    float delta = 0.0f;  // 動かす矩形に加える補正量（画面座標）
    float guide = 0.0f;  // 吸着先の線の座標（ガイド線の描画用）
};

struct Result {
    AxisSnap x;
    AxisSnap y;
};

// threshold: 画面座標での吸着距離。各軸で最も近い線へ独立に吸着する
Result snapRect(const ScreenGuiLayout::Rect& moving,
                const std::vector<ScreenGuiLayout::Rect>& targets,
                float threshold);

// 1本の線（リサイズで動かす辺）を、他の矩形の 始点/中央/終点 の線へ吸着させる。
// horizontal=true なら X 座標（左右の辺）、false なら Y 座標（上下の辺）として扱う
AxisSnap snapLine(float line, const std::vector<ScreenGuiLayout::Rect>& targets,
                  bool horizontal, float threshold);

// リサイズハンドル。x/y は -1=左/上の辺、0=動かさない、+1=右/下の辺（両方0は無効）
struct ResizeHandle {
    int x = 0;
    int y = 0;
};

// 8つのハンドル（四隅と四辺の中点）
const std::vector<ResizeHandle>& resizeHandles();

// ハンドルの画面上の中心
float handleCenterX(const ScreenGuiLayout::Rect& rect, const ResizeHandle& handle);
float handleCenterY(const ScreenGuiLayout::Rect& rect, const ResizeHandle& handle);

// ハンドルを (dx, dy) 動かしたあとの矩形。動かす辺だけを移動し、反対側の辺は固定する。
// 動かす辺を targets へ吸着させ（threshold<=0 なら吸着しない）、幅・高さは minSize 未満にしない。
// snapOut には吸着結果（ガイド線用）を返す
ScreenGuiLayout::Rect resizeRect(const ScreenGuiLayout::Rect& start, const ResizeHandle& handle,
                                 float dx, float dy,
                                 const std::vector<ScreenGuiLayout::Rect>& targets,
                                 float threshold, float minSize, Result& snapOut);

} // namespace GuiSnap
