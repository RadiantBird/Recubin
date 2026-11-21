#include <Util/MeshEdges.hpp>
#include <Math/Vector3.hpp>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace {
    long long quantize(float v) {
        return std::llround(static_cast<double>(v) * 100000.0);
    }

    Vector3 readPosition(const float* vertexData, size_t vertexIndex, size_t stride, size_t positionFloatOffset) {
        const float* base = vertexData + vertexIndex * stride + positionFloatOffset;
        return Vector3(base[0], base[1], base[2]);
    }

    // 量子化座標が同じ頂点を同一視するための作業用レコード
    struct WeldEntry {
        long long x, y, z;
        uint32_t index;
    };

    // 1三角形の1辺。端点は溶接済みIDで、(小さいID<<32 | 大きいID)をキーにする
    struct EdgeRecord {
        uint64_t key;
        uint32_t triangle;
    };

    // 量子化座標の辞書順に溶接IDを振る(IDの大小が元のタプル比較と一致する)。
    // weldId[頂点] = ID、representative[ID] = そのIDで最初の頂点
    void weldVertices(const float* vertexData, size_t vertexCount, size_t stride,
                      size_t positionFloatOffset,
                      std::vector<uint32_t>& weldId, std::vector<uint32_t>& representative) {
        std::vector<WeldEntry> entries;
        entries.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const Vector3 p = readPosition(vertexData, i, stride, positionFloatOffset);
            entries.push_back({quantize(p.x), quantize(p.y), quantize(p.z), static_cast<uint32_t>(i)});
        }
        std::sort(entries.begin(), entries.end(), [](const WeldEntry& a, const WeldEntry& b) {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            if (a.z != b.z) return a.z < b.z;
            return a.index < b.index;
        });

        weldId.assign(vertexCount, 0);
        representative.clear();
        for (size_t i = 0; i < entries.size(); ++i) {
            const bool isNew = (i == 0) ||
                entries[i].x != entries[i - 1].x ||
                entries[i].y != entries[i - 1].y ||
                entries[i].z != entries[i - 1].z;
            if (isNew) representative.push_back(entries[i].index);
            weldId[entries[i].index] = static_cast<uint32_t>(representative.size() - 1);
        }
    }

    Vector3 triangleNormal(const float* vertexData, size_t stride, size_t positionFloatOffset,
                           const unsigned int* indices, size_t triangle) {
        const size_t t = triangle * 3;
        const Vector3 p0 = readPosition(vertexData, indices[t], stride, positionFloatOffset);
        const Vector3 p1 = readPosition(vertexData, indices[t + 1], stride, positionFloatOffset);
        const Vector3 p2 = readPosition(vertexData, indices[t + 2], stride, positionFloatOffset);
        return Vector3::Cross(p1 - p0, p2 - p0).normalize();
    }
}

std::vector<float> MeshEdges::extractHardEdges(const float* vertexData, size_t vertexCount,
                                                size_t stride, size_t positionFloatOffset,
                                                const unsigned int* indices, size_t indexCount,
                                                float creaseAngleDegrees) {
    // 辺ごとにmapノードとvectorを確保すると高ポリゴンで極端に遅いため、
    // 頂点を溶接してIDを振り、辺を1本のvectorに積んでソートして集計する。
    std::vector<uint32_t> weldId;
    std::vector<uint32_t> representative;
    weldVertices(vertexData, vertexCount, stride, positionFloatOffset, weldId, representative);

    const size_t triangleCount = indexCount / 3;
    std::vector<EdgeRecord> edges;
    edges.reserve(triangleCount * 3);
    for (size_t tri = 0; tri < triangleCount; ++tri) {
        const unsigned int idx[3] = {indices[tri * 3], indices[tri * 3 + 1], indices[tri * 3 + 2]};
        if (idx[0] >= vertexCount || idx[1] >= vertexCount || idx[2] >= vertexCount) continue;
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = weldId[idx[e]];
            const uint32_t b = weldId[idx[(e + 1) % 3]];
            const uint32_t lo = std::min(a, b);
            const uint32_t hi = std::max(a, b);
            edges.push_back({(static_cast<uint64_t>(lo) << 32) | hi, static_cast<uint32_t>(tri)});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const EdgeRecord& a, const EdgeRecord& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.triangle < b.triangle;
    });

    double cosThreshold = std::cos(static_cast<double>(creaseAngleDegrees) * pi / 180.0);

    std::vector<float> result;
    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j].key == edges[i].key) ++j;
        const size_t faceCount = j - i;

        bool keep = true;
        if (faceCount == 2) {
            const Vector3 n0 = triangleNormal(vertexData, stride, positionFloatOffset, indices, edges[i].triangle);
            const Vector3 n1 = triangleNormal(vertexData, stride, positionFloatOffset, indices, edges[i + 1].triangle);
            keep = (static_cast<double>(Vector3::Dot(n0, n1)) < cosThreshold);
        }

        if (keep) {
            const uint32_t lo = static_cast<uint32_t>(edges[i].key >> 32);
            const uint32_t hi = static_cast<uint32_t>(edges[i].key & 0xFFFFFFFFu);
            const Vector3 p0 = readPosition(vertexData, representative[lo], stride, positionFloatOffset);
            const Vector3 p1 = readPosition(vertexData, representative[hi], stride, positionFloatOffset);
            result.push_back(p0.x);
            result.push_back(p0.y);
            result.push_back(p0.z);
            result.push_back(p1.x);
            result.push_back(p1.y);
            result.push_back(p1.z);
        }
        i = j;
    }

    return result;
}
