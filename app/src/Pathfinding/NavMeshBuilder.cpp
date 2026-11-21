#include <include/Pathfinding/NavMeshBuilder.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>
#include <include/Math/CFrame.hpp>

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>

#include <cmath>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <limits>
#include <thread>
#include <utility>

namespace Pathfinding {

bool GeometrySnapshot::empty() const {
    return vertices.empty() || triangles.empty();
}

void BuildControl::cancel() {
    cancelRequested.store(true, std::memory_order_release);
}

bool BuildControl::isCancellationRequested() const {
    return cancelRequested.load(std::memory_order_acquire);
}

namespace {

constexpr unsigned char POLYAREA_GROUND = 0;
constexpr unsigned char POLYAREA_JUMP   = 1;
constexpr unsigned short POLYFLAGS_WALK = 0x01;
constexpr unsigned short POLYFLAGS_JUMP = 0x02;

// ワールド空間のOBB(中心cf, 半サイズhalfExtents)を12三角形としてverts/trisへ追加する
void addBoxTriangles(const CFrame& cf, const Vector3& halfExtents,
                      std::vector<float>& verts, std::vector<int>& tris) {
    const float hx = halfExtents.x, hy = halfExtents.y, hz = halfExtents.z;
    const Vector3 local[8] = {
        Vector3(-hx, -hy, -hz), Vector3( hx, -hy, -hz), Vector3( hx, -hy,  hz), Vector3(-hx, -hy,  hz),
        Vector3(-hx,  hy, -hz), Vector3( hx,  hy, -hz), Vector3( hx,  hy,  hz), Vector3(-hx,  hy,  hz),
    };
    const int base = (int)(verts.size() / 3);
    for (const Vector3& lp : local) {
        Vector3 wp = cf.pointToWorld(lp);
        verts.push_back(wp.x);
        verts.push_back(wp.y);
        verts.push_back(wp.z);
    }
    // 各面はワールド外向きになるようCCW巻き順で定義する
    static const int faceTris[12][3] = {
        {0, 1, 2}, {0, 2, 3}, // 底面 (-Y)
        {4, 7, 6}, {4, 6, 5}, // 上面 (+Y)
        {1, 5, 6}, {1, 6, 2}, // +X
        {0, 3, 7}, {0, 7, 4}, // -X
        {2, 6, 7}, {2, 7, 3}, // +Z
        {0, 4, 5}, {0, 5, 1}, // -Z
    };
    for (const auto& t : faceTris) {
        tris.push_back(base + t[0]);
        tris.push_back(base + t[1]);
        tris.push_back(base + t[2]);
    }
}

// Workspaceのサブツリーを再帰的に走査し、Terrainボクセルと静的BaseCubeの
// 三角形ジオメトリ(ワールド空間)を収集する。
void collectGeometry(Instance* node, std::vector<float>& verts, std::vector<int>& tris) {
    if (!node) return;

    if (auto* terrain = dynamic_cast<Terrain*>(node)) {
        if (terrain->streamer) {
            for (const auto& entryPair : terrain->streamer->getChunks()) {
                const Chunk& chunk = entryPair.second.chunk;
                const int base = (int)(verts.size() / 3);
                for (const auto& v : chunk.physVerts) {
                    verts.push_back(v.x);
                    verts.push_back(v.y);
                    verts.push_back(v.z);
                }
                for (size_t i = 0; i + 2 < chunk.physIndices.size(); i += 3) {
                    tris.push_back(base + (int)chunk.physIndices[i]);
                    tris.push_back(base + (int)chunk.physIndices[i + 1]);
                    tris.push_back(base + (int)chunk.physIndices[i + 2]);
                }
            }
        }
    } else if (auto* cube = dynamic_cast<BaseCube*>(node)) {
        if (cube->Anchored && cube->CanCollide) {
            addBoxTriangles(cube->getWorldCFrame(), cube->Size * 0.5f, verts, tris);
        }
    }

    for (auto& [name, child] : node->getChildren()) {
        collectGeometry(child.get(), verts, tris);
    }
}

// ポリメッシュの境界(隣接ポリなし)エッジ。ジャンプリンク候補の判定に使う。
struct BoundaryEdge {
    float mid[3];
    float normal[3]; // 外向き法線（XZ平面のみ、簡易近似）
};

void collectBoundaryEdges(const rcPolyMesh& pmesh, std::vector<BoundaryEdge>& edges) {
    const int nvp = pmesh.nvp;
    for (int i = 0; i < pmesh.npolys; ++i) {
        const unsigned short* p = &pmesh.polys[i * 2 * nvp];

        int nv = 0;
        while (nv < nvp && p[nv] != RC_MESH_NULL_IDX) nv++;
        if (nv < 3) continue;

        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        for (int j = 0; j < nv; ++j) {
            const unsigned short* v = &pmesh.verts[p[j] * 3];
            cx += pmesh.bmin[0] + v[0] * pmesh.cs;
            cy += pmesh.bmin[1] + v[1] * pmesh.ch;
            cz += pmesh.bmin[2] + v[2] * pmesh.cs;
        }
        cx /= (float)nv; cy /= (float)nv; cz /= (float)nv;

        for (int j = 0; j < nv; ++j) {
            // 通常の隣接辺に加え、タイル境界のportal辺も崖として扱わない。
            if (p[nvp + j] != RC_MESH_NULL_IDX) continue;

            const int nj = (j + 1) % nv;
            const unsigned short* v0 = &pmesh.verts[p[j] * 3];
            const unsigned short* v1 = &pmesh.verts[p[nj] * 3];

            float p0[3] = { pmesh.bmin[0] + v0[0] * pmesh.cs, pmesh.bmin[1] + v0[1] * pmesh.ch, pmesh.bmin[2] + v0[2] * pmesh.cs };
            float p1[3] = { pmesh.bmin[0] + v1[0] * pmesh.cs, pmesh.bmin[1] + v1[1] * pmesh.ch, pmesh.bmin[2] + v1[2] * pmesh.cs };

            BoundaryEdge e{};
            e.mid[0] = (p0[0] + p1[0]) * 0.5f;
            e.mid[1] = (p0[1] + p1[1]) * 0.5f;
            e.mid[2] = (p0[2] + p1[2]) * 0.5f;

            float nx = e.mid[0] - cx;
            float nz = e.mid[2] - cz;
            const float nlen = std::sqrt(nx * nx + nz * nz);
            if (nlen > 1e-5f) { nx /= nlen; nz /= nlen; }
            e.normal[0] = nx; e.normal[1] = 0.0f; e.normal[2] = nz;

            edges.push_back(e);
        }
    }
}

struct OffMeshLink {
    float a[3], b[3];
};

// 境界エッジ同士のペアから、ジャンプで繋げる区間を簡易判定する。
// 判定基準: 水平距離がmaxJumpDistance以内・高低差がmaxJumpHeight以内・
// 互いにおおむね向かい合っている（障害物による軌道遮蔽は考慮しない）。
void buildJumpLinks(const std::vector<BoundaryEdge>& edges, const BuildSettings& settings,
                     std::vector<OffMeshLink>& links) {
    const size_t n = edges.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const BoundaryEdge& a = edges[i];
            const BoundaryEdge& b = edges[j];

            const float dx = b.mid[0] - a.mid[0];
            const float dz = b.mid[2] - a.mid[2];
            const float distXZ = std::sqrt(dx * dx + dz * dz);
            if (distXZ < 0.3f || distXZ > settings.maxJumpDistance) continue;

            const float heightDiff = std::fabs(b.mid[1] - a.mid[1]);
            if (heightDiff > settings.maxJumpHeight) continue;

            // 互いに向かい合っているか（法線がおおむね逆向き）
            const float facing = a.normal[0] * b.normal[0] + a.normal[2] * b.normal[2];
            if (facing > -0.3f) continue;

            // Aの外向き法線とA→B方向がおおむね一致しているか（Bが正面にあるか）
            const float dirx = dx / distXZ, dirz = dz / distXZ;
            const float alignA = a.normal[0] * dirx + a.normal[2] * dirz;
            if (alignA < 0.3f) continue;

            OffMeshLink link{};
            link.a[0] = a.mid[0]; link.a[1] = a.mid[1]; link.a[2] = a.mid[2];
            link.b[0] = b.mid[0]; link.b[1] = b.mid[1]; link.b[2] = b.mid[2];
            links.push_back(link);
        }
    }
}

// --- ディスクキャッシュ ---
constexpr uint32_t NAVCACHE_MAGIC   = 0x434D4E52; // "RNMC"
constexpr uint32_t NAVCACHE_VERSION = 3;
constexpr int TILE_SIZE_CELLS = 256;
constexpr float CELL_SIZE = 0.5f;
constexpr float CELL_HEIGHT = 0.25f;
constexpr float TILE_WORLD_SIZE = TILE_SIZE_CELLS * CELL_SIZE;

struct OwnedTileData {
    unsigned char* data = nullptr;
    int size = 0;

    OwnedTileData() = default;
    OwnedTileData(unsigned char* tileData, int tileSize) : data(tileData), size(tileSize) {}
    ~OwnedTileData() { if (data) dtFree(data); }

    OwnedTileData(const OwnedTileData&) = delete;
    OwnedTileData& operator=(const OwnedTileData&) = delete;

    OwnedTileData(OwnedTileData&& other) noexcept : data(other.data), size(other.size) {
        other.data = nullptr;
        other.size = 0;
    }

    OwnedTileData& operator=(OwnedTileData&& other) noexcept {
        if (this == &other) return *this;
        if (data) dtFree(data);
        data = other.data;
        size = other.size;
        other.data = nullptr;
        other.size = 0;
        return *this;
    }

    unsigned char* release() {
        unsigned char* released = data;
        data = nullptr;
        size = 0;
        return released;
    }
};

enum class TileBuildResult {
    Success,
    Empty,
    Failure,
};

uint64_t fnv1aHash(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint32_t normalizedFloatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    // +0.0fと-0.0fはジオメトリとして同一なので同じbit列へ正規化する。
    return (bits & 0x7fffffffU) == 0 ? 0U : bits;
}

// triangleを座標bit列から個別にhashしてsortすることで、Instance/Terrainの
// 列挙順と、それに伴って変化する頂点base indexへの依存を除く。
uint64_t computeGeometryHash(const std::vector<float>& verts, const std::vector<int>& tris,
                              const BuildSettings& settings) {
    const size_t triangleCount = tris.size() / 3;
    std::vector<uint64_t> triangleHashes;
    triangleHashes.reserve(triangleCount);
    for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        uint64_t triangleHash = 14695981039346656037ULL;
        for (size_t corner = 0; corner < 3; ++corner) {
            const int vertexIndex = tris[triangle * 3 + corner];
            for (size_t axis = 0; axis < 3; ++axis) {
                const uint32_t bits =
                    normalizedFloatBits(verts[static_cast<size_t>(vertexIndex) * 3 + axis]);
                triangleHash = fnv1aHash(&bits, sizeof(bits), triangleHash);
            }
        }
        triangleHashes.push_back(triangleHash);
    }
    std::sort(triangleHashes.begin(), triangleHashes.end());

    const uint64_t triangleCount64 = static_cast<uint64_t>(triangleCount);
    uint64_t h = fnv1aHash(&triangleCount64, sizeof(triangleCount64));
    h = fnv1aHash(
        triangleHashes.data(), triangleHashes.size() * sizeof(uint64_t), h);

    // BuildSettingsのpaddingをhashへ含めず、意味のある値だけを固定順で加える。
    const float settingValues[] = {
        settings.agentRadius,
        settings.agentHeight,
        settings.agentMaxClimb,
        settings.agentMaxSlope,
        settings.maxJumpDistance,
        settings.maxJumpHeight,
    };
    for (float value : settingValues) {
        const uint32_t bits = normalizedFloatBits(value);
        h = fnv1aHash(&bits, sizeof(bits), h);
    }
    return h;
}

bool computeNavCapacity(int tileCount, int maxPolyCount, int& maxTiles, int& maxPolys) {
    if (tileCount <= 0 || maxPolyCount <= 0) return false;

    auto nextPow2 = [](int value, int& result, unsigned int& bits) {
        uint32_t power = 1;
        bits = 0;
        while (power < static_cast<uint32_t>(value)) {
            if (power > (std::numeric_limits<uint32_t>::max() >> 1)) return false;
            power <<= 1;
            ++bits;
        }
        if (power > static_cast<uint32_t>(std::numeric_limits<int>::max())) return false;
        result = static_cast<int>(power);
        return true;
    };

    unsigned int tileBits = 0;
    unsigned int polyBits = 0;
    if (!nextPow2(tileCount, maxTiles, tileBits) ||
        !nextPow2(maxPolyCount, maxPolys, polyBits)) {
        return false;
    }

    // 32bit poly refではsaltを最低10bit残す。
    return tileBits + polyBits <= 22;
}

bool validateNavParams(const dtNavMeshParams& params, int tileCount) {
    if (!std::isfinite(params.orig[0]) || !std::isfinite(params.orig[1]) ||
        !std::isfinite(params.orig[2]) || !std::isfinite(params.tileWidth) ||
        !std::isfinite(params.tileHeight)) {
        return false;
    }
    if (params.tileWidth <= 0.0f || params.tileHeight <= 0.0f ||
        params.tileWidth != TILE_WORLD_SIZE || params.tileHeight != TILE_WORLD_SIZE ||
        std::fmod(params.orig[0], TILE_WORLD_SIZE) != 0.0f ||
        std::fmod(params.orig[2], TILE_WORLD_SIZE) != 0.0f ||
        params.maxTiles <= 0 || params.maxPolys <= 0 ||
        tileCount <= 0 || tileCount > params.maxTiles) {
        return false;
    }

    int expectedTiles = 0;
    int expectedPolys = 0;
    return computeNavCapacity(tileCount, params.maxPolys, expectedTiles, expectedPolys) &&
           expectedTiles == params.maxTiles && expectedPolys == params.maxPolys;
}

dtNavMesh* createNavMesh(const dtNavMeshParams& params, std::vector<OwnedTileData>& tiles) {
    dtNavMesh* navMesh = dtAllocNavMesh();
    if (!navMesh || dtStatusFailed(navMesh->init(&params))) {
        if (navMesh) dtFreeNavMesh(navMesh);
        return nullptr;
    }

    for (OwnedTileData& tile : tiles) {
        const int tileSize = tile.size;
        unsigned char* tileData = tile.release();
        if (dtStatusFailed(navMesh->addTile(
                tileData, tileSize, DT_TILE_FREE_DATA, 0, nullptr))) {
            dtFree(tileData);
            dtFreeNavMesh(navMesh);
            return nullptr;
        }
    }
    return navMesh;
}

template<typename T>
bool readCacheValue(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(file);
}

template<typename T>
void writeCacheValue(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

// v2キャッシュはtiled navmeshの初期化値と全tile blobを保持する。
// 破損・旧版・設定不一致は通常のcache missとして扱う。
dtNavMesh* loadNavCache(const std::string& path, uint64_t expectedHash) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fileSize <= 0) return nullptr;

    uint32_t magic = 0, version = 0;
    uint64_t hash = 0;
    dtNavMeshParams params{};
    int32_t maxTiles = 0, maxPolys = 0;
    uint32_t tileCount = 0;
    if (!readCacheValue(file, magic) || !readCacheValue(file, version) ||
        !readCacheValue(file, hash) ||
        !readCacheValue(file, params.orig[0]) ||
        !readCacheValue(file, params.orig[1]) ||
        !readCacheValue(file, params.orig[2]) ||
        !readCacheValue(file, params.tileWidth) ||
        !readCacheValue(file, params.tileHeight) ||
        !readCacheValue(file, maxTiles) ||
        !readCacheValue(file, maxPolys) ||
        !readCacheValue(file, tileCount)) {
        return nullptr;
    }
    params.maxTiles = maxTiles;
    params.maxPolys = maxPolys;
    if (magic != NAVCACHE_MAGIC || version != NAVCACHE_VERSION ||
        hash != expectedHash || tileCount > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        !validateNavParams(params, static_cast<int>(tileCount))) {
        return nullptr;
    }

    const std::streamoff tileDataStart = file.tellg();
    const std::streamoff minimumTileBytes =
        static_cast<std::streamoff>(sizeof(int32_t) + sizeof(dtMeshHeader));
    if (tileDataStart < 0 ||
        static_cast<std::streamoff>(tileCount) > (fileSize - tileDataStart) / minimumTileBytes) {
        return nullptr;
    }

    std::vector<OwnedTileData> tiles;
    tiles.reserve(tileCount);
    int maxPolyCount = 0;
    for (uint32_t i = 0; i < tileCount; ++i) {
        int32_t dataSize = 0;
        if (!readCacheValue(file, dataSize) ||
            dataSize < static_cast<int32_t>(sizeof(dtMeshHeader))) {
            return nullptr;
        }

        const std::streamoff current = file.tellg();
        if (current < 0 || static_cast<std::streamoff>(dataSize) > fileSize - current) {
            return nullptr;
        }

        unsigned char* data = static_cast<unsigned char*>(
            dtAlloc(static_cast<size_t>(dataSize), DT_ALLOC_PERM));
        if (!data) return nullptr;
        file.read(reinterpret_cast<char*>(data), dataSize);
        if (!file) {
            dtFree(data);
            return nullptr;
        }
        const dtMeshHeader* header = reinterpret_cast<const dtMeshHeader*>(data);
        if (header->magic != DT_NAVMESH_MAGIC || header->version != DT_NAVMESH_VERSION ||
            header->layer != 0 || header->polyCount <= 0 ||
            header->polyCount > params.maxPolys) {
            dtFree(data);
            return nullptr;
        }
        maxPolyCount = std::max(maxPolyCount, header->polyCount);
        tiles.emplace_back(data, dataSize);
    }
    if (file.tellg() != fileSize) return nullptr;

    int expectedTiles = 0;
    int expectedPolys = 0;
    if (!computeNavCapacity(
            static_cast<int>(tileCount), maxPolyCount, expectedTiles, expectedPolys) ||
        expectedTiles != params.maxTiles || expectedPolys != params.maxPolys) {
        return nullptr;
    }

    return createNavMesh(params, tiles);
}

void saveNavCache(const std::string& path, uint64_t hash, const dtNavMeshParams& params,
                  const std::vector<OwnedTileData>& tiles) {
    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::filesystem::path tempPath = p;
    tempPath += ".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) return;

    uint32_t magic = NAVCACHE_MAGIC, version = NAVCACHE_VERSION;
    const int32_t maxTiles = params.maxTiles;
    const int32_t maxPolys = params.maxPolys;
    const uint32_t tileCount = static_cast<uint32_t>(tiles.size());
    writeCacheValue(file, magic);
    writeCacheValue(file, version);
    writeCacheValue(file, hash);
    writeCacheValue(file, params.orig[0]);
    writeCacheValue(file, params.orig[1]);
    writeCacheValue(file, params.orig[2]);
    writeCacheValue(file, params.tileWidth);
    writeCacheValue(file, params.tileHeight);
    writeCacheValue(file, maxTiles);
    writeCacheValue(file, maxPolys);
    writeCacheValue(file, tileCount);
    for (const OwnedTileData& tile : tiles) {
        const int32_t dataSize = tile.size;
        writeCacheValue(file, dataSize);
        file.write(reinterpret_cast<const char*>(tile.data), tile.size);
    }
    file.close();
    if (!file) {
        std::filesystem::remove(tempPath, ec);
        return;
    }

    std::filesystem::remove(p, ec);
    ec.clear();
    std::filesystem::rename(tempPath, p, ec);
    if (ec) std::filesystem::remove(tempPath, ec);
}

TileBuildResult buildTile(const std::vector<float>& verts, const std::vector<int>& tris,
                          const std::vector<unsigned char>& triAreas,
                          const BuildSettings& settings, int tileX, int tileY,
                          const float* tileMin, const float* tileMax,
                          OwnedTileData& outTile) {
    const int nverts = static_cast<int>(verts.size() / 3);
    const int ntris = static_cast<int>(tris.size() / 3);

    rcConfig cfg{};
    cfg.cs = CELL_SIZE;
    cfg.ch = CELL_HEIGHT;
    cfg.walkableSlopeAngle     = settings.agentMaxSlope;
    cfg.walkableHeight         = static_cast<int>(std::ceil(settings.agentHeight / cfg.ch));
    cfg.walkableClimb          = static_cast<int>(std::floor(settings.agentMaxClimb / cfg.ch));
    cfg.walkableRadius         = static_cast<int>(std::ceil(settings.agentRadius / cfg.cs));
    cfg.maxEdgeLen             = static_cast<int>(12.0f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea          = static_cast<int>(4.0f * 4.0f);
    cfg.mergeRegionArea        = static_cast<int>(10.0f * 10.0f);
    cfg.maxVertsPerPoly        = 6;
    cfg.detailSampleDist       = cfg.cs * 6.0f;
    cfg.detailSampleMaxError   = cfg.ch;
    cfg.borderSize             = cfg.walkableRadius + 3;
    cfg.width                  = TILE_SIZE_CELLS + cfg.borderSize * 2;
    cfg.height                 = TILE_SIZE_CELLS + cfg.borderSize * 2;

    const float borderWorld = cfg.borderSize * cfg.cs;
    cfg.bmin[0] = tileMin[0] - borderWorld;
    cfg.bmin[1] = tileMin[1];
    cfg.bmin[2] = tileMin[2] - borderWorld;
    cfg.bmax[0] = tileMax[0] + borderWorld;
    cfg.bmax[1] = tileMax[1];
    cfg.bmax[2] = tileMax[2] + borderWorld;

    rcContext ctx(false);
    auto solid = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>(
        rcAllocHeightfield(), &rcFreeHeightField);
    if (!solid || !rcCreateHeightfield(
            &ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        return TileBuildResult::Failure;
    }
    if (!rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), triAreas.data(),
            ntris, *solid, cfg.walkableClimb)) {
        return TileBuildResult::Failure;
    }

    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);
    if (rcGetHeightFieldSpanCount(&ctx, *solid) == 0) return TileBuildResult::Empty;

    auto chf = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>(
        rcAllocCompactHeightfield(), &rcFreeCompactHeightfield);
    if (!chf || !rcBuildCompactHeightfield(
            &ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf)) {
        return TileBuildResult::Failure;
    }
    solid.reset();

    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf) ||
        !rcBuildDistanceField(&ctx, *chf) ||
        !rcBuildRegions(&ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
        return TileBuildResult::Failure;
    }

    auto cset = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>(
        rcAllocContourSet(), &rcFreeContourSet);
    if (!cset || !rcBuildContours(
            &ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) {
        return TileBuildResult::Failure;
    }
    if (cset->nconts == 0) return TileBuildResult::Empty;

    auto pmesh = std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>(
        rcAllocPolyMesh(), &rcFreePolyMesh);
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) {
        return TileBuildResult::Failure;
    }
    if (pmesh->nverts == 0 || pmesh->npolys == 0) return TileBuildResult::Empty;

    auto dmesh = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>(
        rcAllocPolyMeshDetail(), &rcFreePolyMeshDetail);
    if (!dmesh || !rcBuildPolyMeshDetail(
            &ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) {
        return TileBuildResult::Failure;
    }
    chf.reset();
    cset.reset();

    for (int i = 0; i < pmesh->npolys; ++i) {
        if (pmesh->areas[i] == RC_WALKABLE_AREA) {
            pmesh->areas[i] = POLYAREA_GROUND;
            pmesh->flags[i] = POLYFLAGS_WALK;
        }
    }

    // portal辺はcollectBoundaryEdgesで除外されるため、隣接tileとの境界に
    // 重複したジャンプリンクは生成されない。
    std::vector<BoundaryEdge> boundaryEdges;
    collectBoundaryEdges(*pmesh, boundaryEdges);
    std::vector<OffMeshLink> jumpLinks;
    buildJumpLinks(boundaryEdges, settings, jumpLinks);

    std::vector<float> offMeshVerts;
    std::vector<float> offMeshRad;
    std::vector<unsigned short> offMeshFlags;
    std::vector<unsigned char> offMeshAreas;
    std::vector<unsigned char> offMeshDir;
    std::vector<unsigned int> offMeshUserId;
    offMeshVerts.reserve(jumpLinks.size() * 6);
    offMeshRad.reserve(jumpLinks.size());
    offMeshFlags.reserve(jumpLinks.size());
    offMeshAreas.reserve(jumpLinks.size());
    offMeshDir.reserve(jumpLinks.size());
    offMeshUserId.reserve(jumpLinks.size());
    for (size_t i = 0; i < jumpLinks.size(); ++i) {
        const OffMeshLink& link = jumpLinks[i];
        offMeshVerts.insert(offMeshVerts.end(), {
            link.a[0], link.a[1], link.a[2], link.b[0], link.b[1], link.b[2] });
        offMeshRad.push_back(settings.agentRadius);
        offMeshFlags.push_back(POLYFLAGS_JUMP);
        offMeshAreas.push_back(POLYAREA_JUMP);
        offMeshDir.push_back(static_cast<unsigned char>(DT_OFFMESH_CON_BIDIR));
        offMeshUserId.push_back(static_cast<unsigned int>(i + 1));
    }

    dtNavMeshCreateParams params{};
    params.verts      = pmesh->verts;
    params.vertCount  = pmesh->nverts;
    params.polys      = pmesh->polys;
    params.polyAreas  = pmesh->areas;
    params.polyFlags  = pmesh->flags;
    params.polyCount  = pmesh->npolys;
    params.nvp        = pmesh->nvp;
    params.detailMeshes     = dmesh->meshes;
    params.detailVerts      = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris       = dmesh->tris;
    params.detailTriCount   = dmesh->ntris;
    if (!jumpLinks.empty()) {
        params.offMeshConVerts   = offMeshVerts.data();
        params.offMeshConRad     = offMeshRad.data();
        params.offMeshConFlags   = offMeshFlags.data();
        params.offMeshConAreas   = offMeshAreas.data();
        params.offMeshConDir     = offMeshDir.data();
        params.offMeshConUserID  = offMeshUserId.data();
        params.offMeshConCount   = static_cast<int>(jumpLinks.size());
    }
    params.walkableHeight = settings.agentHeight;
    params.walkableRadius = settings.agentRadius;
    params.walkableClimb  = settings.agentMaxClimb;
    params.tileX = tileX;
    params.tileY = tileY;
    params.tileLayer = 0;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        return TileBuildResult::Failure;
    }
    outTile = OwnedTileData(navData, navDataSize);
    return TileBuildResult::Success;
}

void updateBuildControl(
        BuildControl* control, BuildStage stage, float progress,
        uint32_t completedTiles = 0, uint32_t totalTiles = 0) {
    if (!control) return;
    control->stage.store(stage, std::memory_order_release);
    control->completedTiles.store(completedTiles, std::memory_order_relaxed);
    control->totalTiles.store(totalTiles, std::memory_order_relaxed);
    control->progress.store(std::clamp(progress, 0.0f, 1.0f), std::memory_order_release);
}

struct TileInput {
    std::vector<int> triangles;
    std::vector<unsigned char> areas;
};

struct TileOutput {
    TileBuildResult result = TileBuildResult::Empty;
    OwnedTileData tile;
};

} // namespace

NavMesh::~NavMesh() {
    if (m_navQuery) dtFreeNavMeshQuery(m_navQuery);
    if (m_navMesh)  dtFreeNavMesh(m_navMesh);
}

GeometrySnapshot NavMesh::CaptureGeometry(Workspace* workspace) {
    GeometrySnapshot snapshot;
    if (workspace) {
        collectGeometry(workspace, snapshot.vertices, snapshot.triangles);
    }
    return snapshot;
}

std::unique_ptr<NavMesh> NavMesh::Build(Workspace* workspace, const BuildSettings& settings,
                                         const std::string& cachePath) {
    return BuildSnapshot(CaptureGeometry(workspace), settings, cachePath);
}

std::unique_ptr<NavMesh> NavMesh::BuildSnapshot(
        GeometrySnapshot snapshot, const BuildSettings& settings,
        const std::string& cachePath, BuildControl* control) {
    std::vector<float>& verts = snapshot.vertices;
    std::vector<int>& tris = snapshot.triangles;
    const int nverts = (int)(verts.size() / 3);
    const int ntris  = (int)(tris.size() / 3);
    if (nverts == 0 || ntris == 0) {
        updateBuildControl(control, BuildStage::Failed, 1.0f);
        return nullptr;
    }
    if (control && control->isCancellationRequested()) {
        updateBuildControl(control, BuildStage::Cancelled, 1.0f);
        return nullptr;
    }

    updateBuildControl(control, BuildStage::ValidatingCache, 0.02f);
    const uint64_t geomHash = computeGeometryHash(verts, tris, settings);
    dtNavMesh* navMesh = cachePath.empty() ? nullptr : loadNavCache(cachePath, geomHash);
    if (control && control->isCancellationRequested()) {
        if (navMesh) dtFreeNavMesh(navMesh);
        updateBuildControl(control, BuildStage::Cancelled, 1.0f);
        return nullptr;
    }
    if (!navMesh) {
        float globalMin[3], globalMax[3];
        rcCalcBounds(verts.data(), nverts, globalMin, globalMax);

        const float originX = std::floor(globalMin[0] / TILE_WORLD_SIZE) * TILE_WORLD_SIZE;
        const float originZ = std::floor(globalMin[2] / TILE_WORLD_SIZE) * TILE_WORLD_SIZE;
        const double tileCountXDouble = std::ceil(
            (static_cast<double>(globalMax[0]) - originX) / TILE_WORLD_SIZE);
        const double tileCountYDouble = std::ceil(
            (static_cast<double>(globalMax[2]) - originZ) / TILE_WORLD_SIZE);
        if (!std::isfinite(tileCountXDouble) || !std::isfinite(tileCountYDouble) ||
            tileCountXDouble > std::numeric_limits<int>::max() ||
            tileCountYDouble > std::numeric_limits<int>::max()) {
            return nullptr;
        }

        const int tileCountX = std::max(1, static_cast<int>(tileCountXDouble));
        const int tileCountY = std::max(1, static_cast<int>(tileCountYDouble));
        const int64_t gridTileCount =
            static_cast<int64_t>(tileCountX) * static_cast<int64_t>(tileCountY);
        if (gridTileCount <= 0 ||
            gridTileCount > static_cast<int64_t>(std::numeric_limits<int>::max())) {
            updateBuildControl(control, BuildStage::Failed, 1.0f);
            return nullptr;
        }

        rcContext ctx(false);
        std::vector<unsigned char> triAreas(static_cast<size_t>(ntris), 0);
        rcMarkWalkableTriangles(&ctx, settings.agentMaxSlope, verts.data(), nverts,
            tris.data(), ntris, triAreas.data());

        // 各三角形を、Recastのborderを含めて重なるタイルだけへ事前分類する。
        // 各タイルが全三角形を走査していた従来のO(tile * triangle)処理を避ける。
        std::vector<TileInput> tileInputs(static_cast<size_t>(gridTileCount));
        const int borderCells =
            static_cast<int>(std::ceil(settings.agentRadius / CELL_SIZE)) + 3;
        const float borderWorld = static_cast<float>(borderCells) * CELL_SIZE;
        for (int triangle = 0; triangle < ntris; ++triangle) {
            if ((triangle & 1023) == 0 &&
                control && control->isCancellationRequested()) {
                updateBuildControl(control, BuildStage::Cancelled, 1.0f);
                return nullptr;
            }
            const int i0 = tris[static_cast<size_t>(triangle) * 3];
            const int i1 = tris[static_cast<size_t>(triangle) * 3 + 1];
            const int i2 = tris[static_cast<size_t>(triangle) * 3 + 2];
            const float minX = std::min({
                verts[static_cast<size_t>(i0) * 3],
                verts[static_cast<size_t>(i1) * 3],
                verts[static_cast<size_t>(i2) * 3]});
            const float maxX = std::max({
                verts[static_cast<size_t>(i0) * 3],
                verts[static_cast<size_t>(i1) * 3],
                verts[static_cast<size_t>(i2) * 3]});
            const float minZ = std::min({
                verts[static_cast<size_t>(i0) * 3 + 2],
                verts[static_cast<size_t>(i1) * 3 + 2],
                verts[static_cast<size_t>(i2) * 3 + 2]});
            const float maxZ = std::max({
                verts[static_cast<size_t>(i0) * 3 + 2],
                verts[static_cast<size_t>(i1) * 3 + 2],
                verts[static_cast<size_t>(i2) * 3 + 2]});

            const int firstX = std::clamp(
                static_cast<int>(std::floor(
                    (static_cast<double>(minX) - borderWorld - originX) /
                    TILE_WORLD_SIZE)),
                0, tileCountX - 1);
            const int lastX = std::clamp(
                static_cast<int>(std::floor(
                    (static_cast<double>(maxX) + borderWorld - originX) /
                    TILE_WORLD_SIZE)),
                0, tileCountX - 1);
            const int firstY = std::clamp(
                static_cast<int>(std::floor(
                    (static_cast<double>(minZ) - borderWorld - originZ) /
                    TILE_WORLD_SIZE)),
                0, tileCountY - 1);
            const int lastY = std::clamp(
                static_cast<int>(std::floor(
                    (static_cast<double>(maxZ) + borderWorld - originZ) /
                    TILE_WORLD_SIZE)),
                0, tileCountY - 1);

            for (int tileY = firstY; tileY <= lastY; ++tileY) {
                for (int tileX = firstX; tileX <= lastX; ++tileX) {
                    TileInput& input = tileInputs[
                        static_cast<size_t>(tileY) * tileCountX + tileX];
                    input.triangles.push_back(i0);
                    input.triangles.push_back(i1);
                    input.triangles.push_back(i2);
                    input.areas.push_back(triAreas[static_cast<size_t>(triangle)]);
                }
            }
        }

        const uint32_t totalTiles = static_cast<uint32_t>(gridTileCount);
        updateBuildControl(control, BuildStage::BuildingTiles, 0.1f, 0, totalTiles);
        std::vector<TileOutput> tileOutputs(static_cast<size_t>(gridTileCount));
        std::atomic<uint32_t> nextTile{0};
        std::atomic<uint32_t> completedTiles{0};
        std::atomic<bool> buildFailed{false};

        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const unsigned int availableWorkers =
            hardwareThreads > 1 ? hardwareThreads - 1 : 1;
        const unsigned int workerCount = std::max(
            1U, std::min(totalTiles, availableWorkers));
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (unsigned int worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&, totalTiles] {
                while (!buildFailed.load(std::memory_order_acquire)) {
                    if (control && control->isCancellationRequested()) break;
                    const uint32_t tileIndex =
                        nextTile.fetch_add(1, std::memory_order_relaxed);
                    if (tileIndex >= totalTiles) break;

                    const int tileX = static_cast<int>(tileIndex % tileCountX);
                    const int tileY = static_cast<int>(tileIndex / tileCountX);
                    TileInput& input = tileInputs[tileIndex];
                    TileOutput& output = tileOutputs[tileIndex];
                    if (!input.triangles.empty()) {
                        const float tileMin[3] = {
                            originX + tileX * TILE_WORLD_SIZE,
                            globalMin[1],
                            originZ + tileY * TILE_WORLD_SIZE,
                        };
                        const float tileMax[3] = {
                            tileMin[0] + TILE_WORLD_SIZE,
                            globalMax[1],
                            tileMin[2] + TILE_WORLD_SIZE,
                        };
                        output.result = buildTile(
                            verts, input.triangles, input.areas, settings,
                            tileX, tileY, tileMin, tileMax, output.tile);
                        if (output.result == TileBuildResult::Failure) {
                            buildFailed.store(true, std::memory_order_release);
                        }
                    }

                    const uint32_t completed =
                        completedTiles.fetch_add(1, std::memory_order_acq_rel) + 1;
                    if (control) {
                        control->completedTiles.store(completed, std::memory_order_relaxed);
                        control->progress.store(
                            0.1f + 0.8f *
                                (static_cast<float>(completed) /
                                 static_cast<float>(totalTiles)),
                            std::memory_order_release);
                    }
                }
            });
        }
        for (std::thread& worker : workers) worker.join();

        if (control && control->isCancellationRequested()) {
            updateBuildControl(
                control, BuildStage::Cancelled, 1.0f,
                completedTiles.load(std::memory_order_acquire), totalTiles);
            return nullptr;
        }
        if (buildFailed.load(std::memory_order_acquire)) {
            updateBuildControl(
                control, BuildStage::Failed, 1.0f,
                completedTiles.load(std::memory_order_acquire), totalTiles);
            return nullptr;
        }

        // ワーカー完了後に座標順（tileY, tileX）で統合し、スレッドの完了順に
        // 依存しないDetourタイル順とディスクキャッシュを維持する。
        updateBuildControl(
            control, BuildStage::Finalizing, 0.92f, totalTiles, totalTiles);
        std::vector<OwnedTileData> tiles;
        tiles.reserve(static_cast<size_t>(std::min<int64_t>(gridTileCount, 4096)));
        int maxPolyCount = 0;
        for (TileOutput& output : tileOutputs) {
            if (output.result == TileBuildResult::Success) {
                const dtMeshHeader* header =
                    reinterpret_cast<const dtMeshHeader*>(output.tile.data);
                maxPolyCount = std::max(maxPolyCount, header->polyCount);
                tiles.push_back(std::move(output.tile));
            }
        }
        if (tiles.empty()) {
            updateBuildControl(control, BuildStage::Failed, 1.0f, totalTiles, totalTiles);
            return nullptr;
        }

        dtNavMeshParams navParams{};
        navParams.orig[0] = originX;
        navParams.orig[1] = globalMin[1];
        navParams.orig[2] = originZ;
        navParams.tileWidth = TILE_WORLD_SIZE;
        navParams.tileHeight = TILE_WORLD_SIZE;
        if (!computeNavCapacity(static_cast<int>(tiles.size()), maxPolyCount,
                navParams.maxTiles, navParams.maxPolys)) {
            updateBuildControl(control, BuildStage::Failed, 1.0f, totalTiles, totalTiles);
            return nullptr;
        }

        if (control && control->isCancellationRequested()) {
            updateBuildControl(control, BuildStage::Cancelled, 1.0f, totalTiles, totalTiles);
            return nullptr;
        }
        if (!cachePath.empty()) saveNavCache(cachePath, geomHash, navParams, tiles);
        navMesh = createNavMesh(navParams, tiles);
        if (!navMesh) {
            updateBuildControl(control, BuildStage::Failed, 1.0f, totalTiles, totalTiles);
            return nullptr;
        }
    }

    const uint32_t completedTiles = control
        ? control->completedTiles.load(std::memory_order_acquire) : 0;
    const uint32_t totalTiles = control
        ? control->totalTiles.load(std::memory_order_acquire) : 0;
    updateBuildControl(
        control, BuildStage::Finalizing, 0.96f, completedTiles, totalTiles);
    dtNavMeshQuery* navQuery = dtAllocNavMeshQuery();
    if (!navQuery || dtStatusFailed(navQuery->init(navMesh, 2048))) {
        if (navQuery) dtFreeNavMeshQuery(navQuery);
        dtFreeNavMesh(navMesh);
        updateBuildControl(
            control, BuildStage::Failed, 1.0f, completedTiles, totalTiles);
        return nullptr;
    }

    std::unique_ptr<NavMesh> result(new NavMesh());
    result->m_navMesh  = navMesh;
    result->m_navQuery = navQuery;
    updateBuildControl(
        control, BuildStage::Complete, 1.0f, completedTiles, totalTiles);
    return result;
}

std::vector<PathWaypoint> NavMesh::FindPath(const Vector3& start, const Vector3& goal) const {
    std::vector<PathWaypoint> result;
    if (!m_navMesh || !m_navQuery) return result;

    dtQueryFilter filter;
    const float extents[3] = { 4.0f, 4.0f, 4.0f };

    const float startPos[3] = { start.x, start.y, start.z };
    const float goalPos[3]  = { goal.x,  goal.y,  goal.z  };

    dtPolyRef startRef = 0, goalRef = 0;
    float startNearest[3], goalNearest[3];
    if (dtStatusFailed(m_navQuery->findNearestPoly(startPos, extents, &filter, &startRef, startNearest)) || startRef == 0)
        return result;
    if (dtStatusFailed(m_navQuery->findNearestPoly(goalPos, extents, &filter, &goalRef, goalNearest)) || goalRef == 0)
        return result;

    static constexpr int MAX_POLYS = 256;
    dtPolyRef path[MAX_POLYS];
    int pathCount = 0;
    if (dtStatusFailed(m_navQuery->findPath(startRef, goalRef, startNearest, goalNearest, &filter, path, &pathCount, MAX_POLYS)) || pathCount == 0)
        return result;

    static constexpr int MAX_STRAIGHT = 256;
    float straightPath[MAX_STRAIGHT * 3];
    unsigned char straightFlags[MAX_STRAIGHT];
    dtPolyRef straightRefs[MAX_STRAIGHT];
    int straightCount = 0;
    if (dtStatusFailed(m_navQuery->findStraightPath(startNearest, goalNearest, path, pathCount,
            straightPath, straightFlags, straightRefs, &straightCount, MAX_STRAIGHT))) {
        return result;
    }

    result.reserve((size_t)straightCount);
    for (int i = 0; i < straightCount; ++i) {
        PathWaypoint wp;
        wp.Position = Vector3(straightPath[i * 3 + 0], straightPath[i * 3 + 1], straightPath[i * 3 + 2]);
        wp.Action = (straightFlags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ? WaypointAction::Jump : WaypointAction::Walk;
        result.push_back(wp);
    }
    return result;
}

} // namespace Pathfinding
