#pragma once
#include <include/Math/Vector3.hpp>
#include <vector>

namespace SpringHelix {
// 2点p0->p1の間にコイル状の折れ線(フラットな(x,y,z)*N配列)を生成する。
// 両端は軸上の直線リード、中間をcoils回巻き・半径radiusで巻く。
// 2点がほぼ重なる場合は、巻かずに直線(2点)へ退避する。
// segmentsPerCoilは1巻きあたりの分割数（4未満は4に丸める）。coilsは1未満なら1に丸める。
// 先頭頂点はp0、末尾頂点はp1に一致する。
void build(const Vector3& p0, const Vector3& p1, float radius, int coils,
           int segmentsPerCoil, std::vector<float>& outVerts);
}
