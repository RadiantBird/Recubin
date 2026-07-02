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
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace Pathfinding {

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
            if (p[nvp + j] != RC_MESH_NULL_IDX) continue; // 隣接ポリあり = 境界でない

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
constexpr uint32_t NAVCACHE_VERSION = 1;

uint64_t fnv1aHash(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t computeGeometryHash(const std::vector<float>& verts, const std::vector<int>& tris,
                              const BuildSettings& settings) {
    uint64_t h = fnv1aHash(verts.data(), verts.size() * sizeof(float));
    h = fnv1aHash(tris.data(), tris.size() * sizeof(int), h);
    h = fnv1aHash(&settings, sizeof(settings), h);
    return h;
}

// キャッシュファイルからnavDataを読み込む。ヘッダのmagic/version/hashが一致しない、
// またはファイルが無ければfalseを返す。成功時のoutDataはdtAllocで確保されており、
// 呼び出し側はdtNavMesh::init(..., DT_TILE_FREE_DATA)に譲渡するかdtFreeで解放する。
bool loadNavCache(const std::string& path, uint64_t expectedHash, unsigned char*& outData, int& outSize) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    uint32_t magic = 0, version = 0;
    uint64_t hash = 0;
    int32_t size = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&hash), sizeof(hash));
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!file || magic != NAVCACHE_MAGIC || version != NAVCACHE_VERSION || hash != expectedHash || size <= 0)
        return false;

    unsigned char* buf = static_cast<unsigned char*>(dtAlloc(static_cast<size_t>(size), DT_ALLOC_PERM));
    if (!buf) return false;
    file.read(reinterpret_cast<char*>(buf), size);
    if (!file) { dtFree(buf); return false; }

    outData = buf;
    outSize = size;
    return true;
}

void saveNavCache(const std::string& path, uint64_t hash, const unsigned char* data, int size) {
    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return;

    uint32_t magic = NAVCACHE_MAGIC, version = NAVCACHE_VERSION;
    int32_t sz = size;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    file.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    file.write(reinterpret_cast<const char*>(data), size);
}

} // namespace

NavMesh::~NavMesh() {
    if (m_navQuery) dtFreeNavMeshQuery(m_navQuery);
    if (m_navMesh)  dtFreeNavMesh(m_navMesh);
}

std::unique_ptr<NavMesh> NavMesh::Build(Workspace* workspace, const BuildSettings& settings,
                                         const std::string& cachePath) {
    if (!workspace) return nullptr;

    std::vector<float> verts;
    std::vector<int> tris;
    collectGeometry(workspace, verts, tris);

    const int nverts = (int)(verts.size() / 3);
    const int ntris  = (int)(tris.size() / 3);
    if (nverts == 0 || ntris == 0) return nullptr;

    const uint64_t geomHash = computeGeometryHash(verts, tris, settings);

    unsigned char* navData = nullptr;
    int navDataSize = 0;

    if (cachePath.empty() || !loadNavCache(cachePath, geomHash, navData, navDataSize)) {

    rcConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.cs = 0.5f;
    cfg.ch = 0.25f;
    cfg.walkableSlopeAngle     = settings.agentMaxSlope;
    cfg.walkableHeight         = (int)std::ceil(settings.agentHeight / cfg.ch);
    cfg.walkableClimb          = (int)std::floor(settings.agentMaxClimb / cfg.ch);
    cfg.walkableRadius         = (int)std::ceil(settings.agentRadius / cfg.cs);
    cfg.maxEdgeLen             = (int)(12.0f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea          = (int)(4.0f * 4.0f);
    cfg.mergeRegionArea        = (int)(10.0f * 10.0f);
    cfg.maxVertsPerPoly        = 6;
    cfg.detailSampleDist       = cfg.cs * 6.0f;
    cfg.detailSampleMaxError   = cfg.ch * 1.0f;

    rcCalcBounds(verts.data(), nverts, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcContext ctx(false);

    rcHeightfield* solid = rcAllocHeightfield();
    if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        rcFreeHeightField(solid);
        return nullptr;
    }

    std::vector<unsigned char> triAreas((size_t)ntris, 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, triAreas.data());
    if (!rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), triAreas.data(), ntris, *solid, cfg.walkableClimb)) {
        rcFreeHeightField(solid);
        return nullptr;
    }

    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf)) {
        rcFreeHeightField(solid);
        rcFreeCompactHeightfield(chf);
        return nullptr;
    }
    rcFreeHeightField(solid);
    solid = nullptr;

    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf) ||
        !rcBuildDistanceField(&ctx, *chf) ||
        !rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
        rcFreeCompactHeightfield(chf);
        return nullptr;
    }

    rcContourSet* cset = rcAllocContourSet();
    if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) {
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        return nullptr;
    }

    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) {
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        return nullptr;
    }

    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) {
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return nullptr;
    }

    rcFreeCompactHeightfield(chf); chf = nullptr;
    rcFreeContourSet(cset); cset = nullptr;

    for (int i = 0; i < pmesh->npolys; ++i) {
        if (pmesh->areas[i] == RC_WALKABLE_AREA) {
            pmesh->areas[i] = POLYAREA_GROUND;
            pmesh->flags[i] = POLYFLAGS_WALK;
        }
    }

    // --- ジャンプリンク検出（境界エッジ間の簡易ペアリング） ---
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
    for (size_t i = 0; i < jumpLinks.size(); ++i) {
        const OffMeshLink& link = jumpLinks[i];
        offMeshVerts.push_back(link.a[0]); offMeshVerts.push_back(link.a[1]); offMeshVerts.push_back(link.a[2]);
        offMeshVerts.push_back(link.b[0]); offMeshVerts.push_back(link.b[1]); offMeshVerts.push_back(link.b[2]);
        offMeshRad.push_back(settings.agentRadius);
        offMeshFlags.push_back(POLYFLAGS_JUMP);
        offMeshAreas.push_back(POLYAREA_JUMP);
        offMeshDir.push_back((unsigned char)DT_OFFMESH_CON_BIDIR);
        offMeshUserId.push_back((unsigned int)i);
    }

    dtNavMeshCreateParams params;
    std::memset(&params, 0, sizeof(params));
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
        params.offMeshConCount   = (int)jumpLinks.size();
    }
    params.walkableHeight = settings.agentHeight;
    params.walkableRadius = settings.agentRadius;
    params.walkableClimb  = settings.agentMaxClimb;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    const bool created = dtCreateNavMeshData(&params, &navData, &navDataSize);

    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    if (!created) return nullptr;

    if (!cachePath.empty()) saveNavCache(cachePath, geomHash, navData, navDataSize);

    } // if (cachePath.empty() || !loadNavCache(...))

    dtNavMesh* navMesh = dtAllocNavMesh();
    if (!navMesh || dtStatusFailed(navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA))) {
        dtFree(navData);
        if (navMesh) dtFreeNavMesh(navMesh);
        return nullptr;
    }

    dtNavMeshQuery* navQuery = dtAllocNavMeshQuery();
    if (!navQuery || dtStatusFailed(navQuery->init(navMesh, 2048))) {
        if (navQuery) dtFreeNavMeshQuery(navQuery);
        dtFreeNavMesh(navMesh);
        return nullptr;
    }

    std::unique_ptr<NavMesh> result(new NavMesh());
    result->m_navMesh  = navMesh;
    result->m_navQuery = navQuery;
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
