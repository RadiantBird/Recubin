#pragma once
#include <vector>
#include <cstddef>

// 三角形メッシュから「硬いエッジ」(本来の稜線)のみを抽出するユーティリティ。
// 用途: ハイライト輪郭線の描画用に、内部の三角形分割対角線を除いた見た目の良いワイヤーフレームを得る。
namespace MeshEdges {
    // vertexData: 頂点属性がインターリーブされたfloat配列。stride: 1頂点あたりのfloat数。
    // positionFloatOffset: 1頂点内での位置(x,y,z)の開始オフセット(float単位)。
    // indices: 三角形インデックス配列(長さは3の倍数)。
    // creaseAngleDegrees: 隣接する2三角形の法線のなす角がこの値を超えるエッジ、または
    //                     隣接三角形が1つしかない(境界)エッジのみを「硬いエッジ」として残す。
    // 戻り値: ローカル座標のフラット配列(x0,y0,z0,x1,y1,z1の繰り返し)。独立したセグメント群であり、
    //        連結ポリラインではない。
    std::vector<float> extractHardEdges(const float* vertexData, size_t vertexCount,
                                         size_t stride, size_t positionFloatOffset,
                                         const unsigned int* indices, size_t indexCount,
                                         float creaseAngleDegrees = 20.0f);
}
