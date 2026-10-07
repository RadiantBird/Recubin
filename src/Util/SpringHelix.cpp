#include <include/Util/SpringHelix.hpp>
#include <algorithm>
#include <cmath>

namespace {
void pushPoint(std::vector<float>& out, const Vector3& p) {
    out.push_back(p.x);
    out.push_back(p.y);
    out.push_back(p.z);
}

// axis(正規化済み)に直交する正規直交基底(u, v)を作る
void orthonormalBasis(const Vector3& axis, Vector3& u, Vector3& v) {
    const Vector3 helper = (std::fabs(axis.y) < 0.9f) ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
    u = Vector3::Cross(axis, helper).normalize();
    v = Vector3::Cross(axis, u);
}
}

void SpringHelix::build(const Vector3& p0, const Vector3& p1, float radius, int coils,
                        int segmentsPerCoil, std::vector<float>& outVerts) {
    outVerts.clear();
    const Vector3 delta = p1 - p0;
    const float length = delta.length();
    if (!std::isfinite(length) || length < 1.0e-4f || radius <= 0.0f) {
        pushPoint(outVerts, p0);
        pushPoint(outVerts, p1);
        return;
    }
    const Vector3 axis = delta / length;
    Vector3 u, v;
    orthonormalBasis(axis, u, v);

    coils = std::max(coils, 1);
    segmentsPerCoil = std::max(segmentsPerCoil, 4);
    // 両端のリード長: 全長の10%（ただし半径以上にはしない）。巻き部分が負にならないよう制限する。
    const float lead = std::min(length * 0.1f, radius * 2.0f);
    const float coilLength = std::max(length - 2.0f * lead, 0.0f);
    const int steps = coils * segmentsPerCoil;
    const float twoPi = 6.28318530718f;

    outVerts.reserve(static_cast<size_t>(steps + 3) * 3);
    pushPoint(outVerts, p0);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float angle = t * static_cast<float>(coils) * twoPi;
        // 巻き始め/巻き終わりで半径を0へ絞り、リード直線へ滑らかにつなぐ
        const float taper = std::min(1.0f, std::min(t, 1.0f - t) * 8.0f);
        const Vector3 center = p0 + axis * (lead + coilLength * t);
        pushPoint(outVerts, center + (u * std::cos(angle) + v * std::sin(angle)) * (radius * taper));
    }
    pushPoint(outVerts, p1);
}
