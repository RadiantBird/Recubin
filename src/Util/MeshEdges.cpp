#include <Util/MeshEdges.hpp>
#include <Math/Vector3.hpp>
#include <map>
#include <tuple>
#include <cmath>

namespace {
    long long quantize(float v) {
        return std::llround(static_cast<double>(v) * 100000.0);
    }

    using EdgeKey = std::tuple<long long, long long, long long, long long, long long, long long>;

    struct EdgeInfo {
        Vector3 p0;
        Vector3 p1;
        std::vector<Vector3> faceNormals;
    };

    Vector3 readPosition(const float* vertexData, size_t vertexIndex, size_t stride, size_t positionFloatOffset) {
        const float* base = vertexData + vertexIndex * stride + positionFloatOffset;
        return Vector3(base[0], base[1], base[2]);
    }
}

std::vector<float> MeshEdges::extractHardEdges(const float* vertexData, size_t vertexCount,
                                                size_t stride, size_t positionFloatOffset,
                                                const unsigned int* indices, size_t indexCount,
                                                float creaseAngleDegrees) {
    std::map<EdgeKey, EdgeInfo> edgeMap;

    for (size_t t = 0; t + 2 < indexCount; t += 3) {
        unsigned int i0 = indices[t];
        unsigned int i1 = indices[t + 1];
        unsigned int i2 = indices[t + 2];

        Vector3 p0 = readPosition(vertexData, i0, stride, positionFloatOffset);
        Vector3 p1 = readPosition(vertexData, i1, stride, positionFloatOffset);
        Vector3 p2 = readPosition(vertexData, i2, stride, positionFloatOffset);

        Vector3 faceNormal = Vector3::Cross(p1 - p0, p2 - p0).normalize();

        Vector3 edges[3][2] = {
            {p0, p1},
            {p1, p2},
            {p2, p0}
        };

        for (int e = 0; e < 3; ++e) {
            Vector3 a = edges[e][0];
            Vector3 b = edges[e][1];

            long long ax = quantize(a.x), ay = quantize(a.y), az = quantize(a.z);
            long long bx = quantize(b.x), by = quantize(b.y), bz = quantize(b.z);

            std::tuple<long long, long long, long long> qa(ax, ay, az);
            std::tuple<long long, long long, long long> qb(bx, by, bz);

            EdgeKey key;
            Vector3 firstP, secondP;
            if (qa < qb) {
                key = EdgeKey(ax, ay, az, bx, by, bz);
                firstP = a;
                secondP = b;
            } else {
                key = EdgeKey(bx, by, bz, ax, ay, az);
                firstP = b;
                secondP = a;
            }

            auto it = edgeMap.find(key);
            if (it == edgeMap.end()) {
                EdgeInfo info;
                info.p0 = firstP;
                info.p1 = secondP;
                info.faceNormals.push_back(faceNormal);
                edgeMap.emplace(key, info);
            } else {
                it->second.faceNormals.push_back(faceNormal);
            }
        }
    }

    double cosThreshold = std::cos(static_cast<double>(creaseAngleDegrees) * pi / 180.0);

    std::vector<float> result;
    for (const auto& kv : edgeMap) {
        const EdgeInfo& info = kv.second;
        bool keep = false;

        if (info.faceNormals.size() == 1) {
            keep = true;
        } else if (info.faceNormals.size() == 2) {
            float d = Vector3::Dot(info.faceNormals[0], info.faceNormals[1]);
            keep = (static_cast<double>(d) < cosThreshold);
        } else {
            keep = true;
        }

        if (keep) {
            result.push_back(info.p0.x);
            result.push_back(info.p0.y);
            result.push_back(info.p0.z);
            result.push_back(info.p1.x);
            result.push_back(info.p1.y);
            result.push_back(info.p1.z);
        }
    }

    return result;
}
