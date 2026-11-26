#pragma once

#include <Math/Vector3.hpp>

#include <vector>

// 独立した(連結ポリラインでない)線分群を、画面上で常に一定ピクセル幅に見えるリボンに変換する。
// segmentEndpoints: ワールド空間、セグメントごとに(x0,y0,z0,x1,y1,z1)のフラット配列。
// fovYDegrees/viewportHeightPxはMatrix4::Perspective()と同じ規約(垂直視野角・ピクセル高さ)。
void buildSegmentRibbons(const std::vector<float>& segmentEndpoints,
                         const Vector3& cameraPosition, float fovYDegrees,
                         int viewportHeightPx, float pixelWidth,
                         std::vector<float>& outVertices);

// 連結ポリライン(フラットな(x,y,z)*N配列)を、カメラ向きのリボン三角形(GL_TRIANGLES用フラット頂点配列)に
// 変換する。ワールド空間の全幅(width)を使う。各頂点で前後セグメント方向を平均した接線を使うため、
// ジョイント部に隙間ができない。position-onlyの頂点入力へそのまま渡せる。
void buildRibbonStrip(const std::vector<float>& points, float width,
                      const Vector3& cameraPosition,
                      std::vector<float>& outVertices);
