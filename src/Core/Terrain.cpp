#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>
#include <Instances/Workspace.hpp>
#include <vector>
#include <cstring>
#include <cmath>

// ================================================================== //
//  ローカル座標系
//
//      6---7
//     /|  /|
//    4---5 |
//    | 2-|-3
//    |/  |/
//    0---1
//
//  0:(-1,-1,-1)  1:(+1,-1,-1)
//  2:(-1,-1,+1)  3:(+1,-1,+1)
//  4:(-1,+1,-1)  5:(+1,+1,-1)
//  6:(-1,+1,+1)  7:(+1,+1,+1)
//
//  N=-Z, S=+Z, E=+X, W=-X  （右手系、Y上）
// ================================================================== //

namespace {

static const float BASE_VERTS[8][3] = {
    {-1,-1,-1}, {+1,-1,-1},
    {-1,-1,+1}, {+1,-1,+1},
    {-1,+1,-1}, {+1,+1,-1},
    {-1,+1,+1}, {+1,+1,+1},
};

struct FaceDef {
    int   vi[4];
    float nx, ny, nz;
};

static const FaceDef CUBE_FACES[6] = {
    {{4,5,7,6},  0, 1, 0},  // Top    (+Y)
    {{0,2,3,1},  0,-1, 0},  // Bottom (-Y)
    {{2,6,7,3},  0, 0, 1},  // South  (+Z)
    {{0,1,5,4},  0, 0,-1},  // North  (-Z)
    {{1,3,7,5},  1, 0, 0},  // East   (+X)
    {{0,4,6,2}, -1, 0, 0},  // West   (-X)
};

static constexpr int FACE_TOP    = 0;
static constexpr int FACE_BOTTOM = 1;
static constexpr int FACE_SOUTH  = 2;
static constexpr int FACE_NORTH  = 3;
static constexpr int FACE_EAST   = 4;
static constexpr int FACE_WEST   = 5;

// --- 形状ごとの三角形インデックステーブル ---
static const int TRI_CUBE[] = {
    4,5,7, 4,7,6,
    0,3,1, 0,2,3,
    2,7,3, 2,6,7,
    0,1,5, 0,5,4,
    1,3,7, 1,7,5,
    0,6,2, 0,4,6,
};
static const int TRI_WEDGE_TopNE[] = {
    0,3,1, 0,2,3,
    4,5,6,
    2,6,3,
    1,3,5,
    0,1,5, 0,5,4,
    0,4,6, 0,6,2,
    3,6,5,
};
static const int TRI_WEDGE_TopNW[] = {
    // 頂点4(上面NW)を除去する楔。TopSE をX軸ミラー（0↔1,2↔3,4↔5,6↔7＋巻き反転）して生成。
    0,2,1, 2,3,1,    // 底面
    7,6,5,           // 上面（頂点4を除く三角形）
    5,0,1,           // 北面(-Z) 三角形
    6,2,0,           // 西面(-X) 三角形（フル面ではなく頂点4を欠いた三角形）
    5,6,0,           // 斜面（カット面）
    2,6,3, 6,7,3,    // 南面(+Z) 四角形
    7,5,1, 3,7,1,    // 東面(+X) 四角形
};
static const int TRI_WEDGE_TopSE[] = {
    0,3,1, 0,2,3,
    4,7,6,
    0,1,4,
    1,3,7, 1,7,4,
    2,7,3, 2,6,7,
    0,4,6, 0,6,2,
};
static const int TRI_WEDGE_TopSW[] = {
    // 頂点6(上面SW)を除去する楔。TopSE をY軸まわり180°回転（0↔3,1↔2,4↔7,5↔6）して生成。
    // 旧定義は斜面(頂点4,7,2)が欠落しており、カット部分が穴になって内部が透けていた。
    3,0,2, 3,1,0,    // 底面
    7,4,5,           // 上面（頂点6を除く三角形）
    3,2,7,           // 南面(+Z) 三角形
    2,0,4, 2,4,7,    // 西面(-X) 三角形 + 斜面（カット面）
    1,4,0, 1,5,4,    // 北面(-Z) 四角形
    3,7,5, 3,5,1,    // 東面(+X) 四角形
};
static const int TRI_WEDGE_BotNE[] = {
    4,5,7, 4,7,6,
    0,2,4,
    0,4,5,
    0,5,4,
    2,6,0, 0,6,4,
    2,6,7,
    0,5,4,
    0,2,6, 0,6,4,
};
static const int TRI_WEDGE_BotNW[] = {
    4,5,7, 4,7,6,
    1,5,4,
    6,7,3,
    1,5,4,
    1,3,7, 1,7,5,
    1,4,6, 1,6,3,
};
static const int TRI_WEDGE_BotSE[] = {
    5,7,6,
    1,3,7, 1,7,5,
    0,1,5,
    6,7,3, 6,3,0,
    0,5,6,
};
static const int TRI_WEDGE_BotSW[] = {
    4,7,6,
    0,4,6, 0,6,2,
    0,1,4,
    2,6,7,
    1,4,0, 1,7,4,
};
static const int TRI_TETRA_TopNE[] = {
    0,3,1, 0,2,3,
    1,3,7,
    2,7,3,
    0,1,7, 0,7,2,
};
static const int TRI_TETRA_TopNW[] = {
    0,3,1, 0,2,3,
    0,1,4,
    0,4,2,
    1,4,0, 1,3,4,
    2,4,3,
};
static const int TRI_TETRA_TopSE[] = {
    0,3,1, 0,2,3,
    0,1,5, 0,5,4,
    1,3,5,
    0,5,1, 2,3,5,
    0,2,5,
};
static const int TRI_TETRA_TopSW[] = {
    0,3,1, 0,2,3,
    2,6,3,
    0,6,2,
    0,1,6, 1,3,6,
};
static const int TRI_TETRA_BotNE[] = {
    4,5,7, 4,7,6,
    1,7,5,
    1,5,4, 1,4,6, 1,6,7,
};
static const int TRI_TETRA_BotNW[] = {
    4,5,7, 4,7,6,
    0,4,6,
    0,6,7, 0,7,5, 0,5,4,
};
static const int TRI_TETRA_BotSE[] = {
    4,5,7, 4,7,6,
    3,7,5,
    3,6,7,
    3,5,4, 3,4,6,
};
static const int TRI_TETRA_BotSW[] = {
    4,5,7, 4,7,6,
    2,6,7,
    2,4,6,
    2,7,4, 2,5,7,
    2,4,5,
};

static const int* SHAPE_TRIS[] = {
    nullptr,
    TRI_CUBE,
    TRI_WEDGE_TopNE, TRI_WEDGE_TopNW, TRI_WEDGE_TopSE, TRI_WEDGE_TopSW,
    TRI_WEDGE_BotNE, TRI_WEDGE_BotNW, TRI_WEDGE_BotSE, TRI_WEDGE_BotSW,
    TRI_TETRA_TopNE, TRI_TETRA_TopNW, TRI_TETRA_TopSE, TRI_TETRA_TopSW,
    TRI_TETRA_BotNE, TRI_TETRA_BotNW, TRI_TETRA_BotSE, TRI_TETRA_BotSW,
};
static const int SHAPE_TRI_COUNTS[] = {
    0,
    (int)(sizeof(TRI_CUBE)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_TopNE)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_TopNW)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_TopSE)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_TopSW)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_BotNE)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_BotNW)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_BotSE)/sizeof(int)),
    (int)(sizeof(TRI_WEDGE_BotSW)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_TopNE)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_TopNW)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_TopSE)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_TopSW)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_BotNE)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_BotNW)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_BotSE)/sizeof(int)),
    (int)(sizeof(TRI_TETRA_BotSW)/sizeof(int)),
};

static bool shouldSkipFace(const Chunk& chunk, const TerrainStreamer* streamer, int x, int y, int z, int face)
{
    int nx = x, ny = y, nz = z;
    switch (face) {
        case FACE_TOP:    ny++; break;
        case FACE_BOTTOM: ny--; break;
        case FACE_SOUTH:  nz++; break;
        case FACE_NORTH:  nz--; break;
        case FACE_EAST:   nx++; break;
        case FACE_WEST:   nx--; break;
    }
    if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE) {
        return chunk.blocks[nx][ny][nz].shape == BlockShape::Cube;
    }

    if (streamer) {
        int32_t wx = chunk.worldOriginX() + nx;
        int32_t wy = chunk.worldOriginY() + ny;
        int32_t wz = chunk.worldOriginZ() + nz;
        const Block* nb = streamer->getBlockGlobal(wx, wy, wz);
        if (nb) {
            return nb->shape == BlockShape::Cube;
        }
    }
    return false;
}

// 隣接ブロックが非空（=何かしらの形状で埋まっている）か。ランプの側面三角カリング用。
static bool neighborNonEmpty(const Chunk& chunk, const TerrainStreamer* streamer, int x, int y, int z, int face)
{
    int nx = x, ny = y, nz = z;
    switch (face) {
        case FACE_TOP:    ny++; break;
        case FACE_BOTTOM: ny--; break;
        case FACE_SOUTH:  nz++; break;
        case FACE_NORTH:  nz--; break;
        case FACE_EAST:   nx++; break;
        case FACE_WEST:   nx--; break;
    }
    if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE) {
        return chunk.blocks[nx][ny][nz].shape != BlockShape::Empty;
    }
    if (streamer) {
        const Block* nb = streamer->getBlockGlobal(chunk.worldOriginX() + nx,
                                                   chunk.worldOriginY() + ny,
                                                   chunk.worldOriginZ() + nz);
        if (nb) return nb->shape != BlockShape::Empty;
    }
    return false;
}

// 三角柱（エッジランプ）の面構成。slope=斜面(常時描画), highFace=高い側のフル面(Cube隣接でカリング),
// sideA/sideB=側面の三角(非空隣接でカリング)。底面は共通 RAMP_BOTTOM(Cube隣接でカリング)。
// 巻きは外向き法線（slope は +Y 成分あり=上向き）になるよう手計算済み。
struct RampGeom {
    int slope[6];
    int highFace[6]; int highCull;
    int sideA[3];    int sideACull;
    int sideB[3];    int sideBCull;
};
static const RampGeom RAMP_GEOM[4] = {
    // Ramp_N（低=North, 高=South）
    { {0,7,1, 0,6,7}, {2,7,6, 2,3,7}, FACE_SOUTH, {1,7,3}, FACE_EAST,  {0,2,6}, FACE_WEST  },
    // Ramp_S（低=South, 高=North）
    { {2,3,5, 2,5,4}, {0,5,1, 0,4,5}, FACE_NORTH, {1,5,3}, FACE_EAST,  {0,2,4}, FACE_WEST  },
    // Ramp_E（低=East,  高=West）
    { {1,6,3, 1,4,6}, {0,6,4, 0,2,6}, FACE_WEST,  {0,4,1}, FACE_NORTH, {2,3,6}, FACE_SOUTH },
    // Ramp_W（低=West,  高=East）
    { {0,2,7, 0,7,5}, {1,7,3, 1,5,7}, FACE_EAST,  {0,5,1}, FACE_NORTH, {2,3,7}, FACE_SOUTH },
};
static const int RAMP_BOTTOM[6] = { 0,3,1, 0,2,3 };

static bool isRampShape(BlockShape shape)
{
    return shape >= BlockShape::Ramp_N && shape <= BlockShape::Ramp_W;
}

static bool getRampConvexVertexIndices(BlockShape shape, int out[6])
{
    static const int RAMP_VERTS[4][6] = {
        {0, 1, 2, 3, 6, 7},
        {0, 1, 2, 3, 4, 5},
        {0, 1, 2, 3, 4, 6},
        {0, 1, 2, 3, 5, 7},
    };
    if (!isRampShape(shape)) return false;
    const int rampIndex = (int)shape - (int)BlockShape::Ramp_N;
    for (int i = 0; i < 6; i++) out[i] = RAMP_VERTS[rampIndex][i];
    return true;
}

static int getConvexVertexIndices(BlockShape shape, int out[8])
{
    int rampIndices[6];
    if (getRampConvexVertexIndices(shape, rampIndices)) {
        for (int i = 0; i < 6; ++i) out[i] = rampIndices[i];
        return 6;
    }

    const uint8_t shapeIndex = static_cast<uint8_t>(shape);
    if (shapeIndex >= sizeof(SHAPE_TRIS) / sizeof(SHAPE_TRIS[0])) return 0;
    const int* triangles = SHAPE_TRIS[shapeIndex];
    const int triangleIndexCount = SHAPE_TRI_COUNTS[shapeIndex];
    if (!triangles || triangleIndexCount == 0) return 0;

    bool used[8] = {};
    int count = 0;
    for (int i = 0; i < triangleIndexCount; ++i) {
        const int vertexIndex = triangles[i];
        if (vertexIndex < 0 || vertexIndex >= 8 || used[vertexIndex]) continue;
        used[vertexIndex] = true;
        out[count++] = vertexIndex;
    }
    return count;
}

static void calcNormal(const float* a, const float* b, const float* c,
                       float& onx, float& ony, float& onz)
{
    float ax = b[0]-a[0], ay = b[1]-a[1], az = b[2]-a[2];
    float bx = c[0]-a[0], by = c[1]-a[1], bz = c[2]-a[2];
    onx = ay*bz - az*by;
    ony = az*bx - ax*bz;
    onz = ax*by - ay*bx;
    float len = sqrtf(onx*onx + ony*ony + onz*onz);
    if (len > 1e-6f) { onx/=len; ony/=len; onz/=len; }
}

static void pushVertex(std::vector<TerrainVertex>& verts,
                       float wx, float wy, float wz,
                       float nx, float ny, float nz,
                       float u,  float v,
                       uint8_t r, uint8_t g, uint8_t b)
{
    verts.push_back({wx,wy,wz, nx,ny,nz, u,v, r,g,b,255});
}

} // namespace

// ================================================================== //
//  buildChunkMesh
//  描画用 VAO/VBO/EBO を生成し、同時に physVerts / physIndices を埋める。
// ================================================================== //
void buildChunkMesh(Chunk& chunk, const TerrainStreamer* streamer)
{
    std::vector<TerrainVertex>    verts;
    std::vector<uint32_t>         indices;
    std::vector<Vector3>          physVerts;
    std::vector<uint32_t>         physIndices;
    std::vector<ConvexBlock>      physConvexBlocks;

    verts.reserve(4096);
    indices.reserve(8192);
    physVerts.reserve(4096);
    physIndices.reserve(8192);

    static constexpr float BS = TerrainStreamer::BLOCK_STUD_SIZE;
    const Vector3 chunkOriginStuds(
        static_cast<float>(chunk.worldOriginX()) * BS,
        static_cast<float>(chunk.worldOriginY()) * BS,
        static_cast<float>(chunk.worldOriginZ()) * BS);

    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int y = 0; y < CHUNK_SIZE; y++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    {
        const Block& blk = chunk.blocks[x][y][z];
        if (blk.isEmpty()) continue;

        const uint8_t fr = blk.r;
        const uint8_t fg = blk.g;
        const uint8_t fb = blk.b;

        static constexpr float BHS = BS * 0.5f; // ブロック半サイズ

        const float ox = (float)(chunk.worldOriginX() + x) * BS;
        const float oy = (float)(chunk.worldOriginY() + y) * BS;
        const float oz = (float)(chunk.worldOriginZ() + z) * BS;

        const uint8_t shapeIdx = static_cast<uint8_t>(blk.shape);

        if (blk.shape == BlockShape::Cube)
        {
            for (int f = 0; f < 6; f++)
            {
                if (shouldSkipFace(chunk, streamer, x, y, z, f)) continue;

                const FaceDef& fd = CUBE_FACES[f];
                const uint32_t base     = (uint32_t)verts.size();
                const uint32_t physBase = (uint32_t)physVerts.size();

                for (int vi = 0; vi < 4; vi++)
                {
                    const float* lv = BASE_VERTS[fd.vi[vi]];
                    float wx = ox + lv[0]*BHS;
                    float wy = oy + lv[1]*BHS;
                    float wz = oz + lv[2]*BHS;
                    pushVertex(verts, wx, wy, wz,
                        fd.nx, fd.ny, fd.nz,
                        (vi==0||vi==3)?0.0f:1.0f,
                        (vi<2)?0.0f:1.0f,
                        fr, fg, fb);
                    physVerts.emplace_back(
                        wx - chunkOriginStuds.x,
                        wy - chunkOriginStuds.y,
                        wz - chunkOriginStuds.z);
                }
                // 描画用
                indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
                indices.push_back(base+0); indices.push_back(base+2); indices.push_back(base+3);
                // 物理用（ワインディング反転 → 法線を外向きに修正）
                physIndices.push_back(physBase+2); physIndices.push_back(physBase+1); physIndices.push_back(physBase+0);
                physIndices.push_back(physBase+3); physIndices.push_back(physBase+2); physIndices.push_back(physBase+0);
            }
        }
        else if (shapeIdx < (uint8_t)(sizeof(SHAPE_TRIS)/sizeof(SHAPE_TRIS[0])))
        {
            const int*  tris  = SHAPE_TRIS[shapeIdx];
            const int   count = SHAPE_TRI_COUNTS[shapeIdx];
            if (!tris || count == 0) continue;
            physConvexBlocks.push_back({
                shapeIdx,
                Vector3(
                    ox - chunkOriginStuds.x,
                    oy - chunkOriginStuds.y,
                    oz - chunkOriginStuds.z)
            });

            for (int ti = 0; ti + 2 < count; ti += 3)
            {
                const float* va = BASE_VERTS[tris[ti+0]];
                const float* vb = BASE_VERTS[tris[ti+1]];
                const float* vc = BASE_VERTS[tris[ti+2]];

                float wa[3] = { ox+va[0]*BHS, oy+va[1]*BHS, oz+va[2]*BHS };
                float wb[3] = { ox+vb[0]*BHS, oy+vb[1]*BHS, oz+vb[2]*BHS };
                float wc[3] = { ox+vc[0]*BHS, oy+vc[1]*BHS, oz+vc[2]*BHS };

                float nx, ny, nz;
                calcNormal(wa, wb, wc, nx, ny, nz);

                const uint32_t base     = (uint32_t)verts.size();
                pushVertex(verts, wa[0],wa[1],wa[2], nx,ny,nz, 0.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wb[0],wb[1],wb[2], nx,ny,nz, 1.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wc[0],wc[1],wc[2], nx,ny,nz, 0.5f,1.0f, fr,fg,fb);

                indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
            }
        }
        else if (isRampShape(blk.shape))
        {
            // 三角柱（エッジランプ）: 面ごとにカリングして連続ランプの重なり/Zファイトを防ぐ。
            auto emitTri = [&](int ia, int ib, int ic, bool visual = true) {
                if (!visual) return;
                const float* va = BASE_VERTS[ia];
                const float* vb = BASE_VERTS[ib];
                const float* vc = BASE_VERTS[ic];
                float wa[3] = { ox+va[0]*BHS, oy+va[1]*BHS, oz+va[2]*BHS };
                float wb[3] = { ox+vb[0]*BHS, oy+vb[1]*BHS, oz+vb[2]*BHS };
                float wc[3] = { ox+vc[0]*BHS, oy+vc[1]*BHS, oz+vc[2]*BHS };
                float nx, ny, nz;
                calcNormal(wa, wb, wc, nx, ny, nz);
                const uint32_t base = (uint32_t)verts.size();
                pushVertex(verts, wa[0],wa[1],wa[2], nx,ny,nz, 0.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wb[0],wb[1],wb[2], nx,ny,nz, 1.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wc[0],wc[1],wc[2], nx,ny,nz, 0.5f,1.0f, fr,fg,fb);
                indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
            };

            // 当たり判定は凸包で作る（三角形物理は出さない）
            physConvexBlocks.push_back({
                shapeIdx,
                Vector3(
                    ox - chunkOriginStuds.x,
                    oy - chunkOriginStuds.y,
                    oz - chunkOriginStuds.z)
            });

            const RampGeom& rg = RAMP_GEOM[(int)blk.shape - (int)BlockShape::Ramp_N];
            // 斜面（常時）
            emitTri(rg.slope[0], rg.slope[1], rg.slope[2]);
            emitTri(rg.slope[3], rg.slope[4], rg.slope[5]);
            // 底面（下が Cube なら省略）
            const bool drawBottom = !shouldSkipFace(chunk, streamer, x, y, z, FACE_BOTTOM);
            emitTri(RAMP_BOTTOM[0], RAMP_BOTTOM[1], RAMP_BOTTOM[2], drawBottom);
            emitTri(RAMP_BOTTOM[3], RAMP_BOTTOM[4], RAMP_BOTTOM[5], drawBottom);
            // 高い側のフル面（隣が Cube なら省略）
            const bool drawHigh = !shouldSkipFace(chunk, streamer, x, y, z, rg.highCull);
            emitTri(rg.highFace[0], rg.highFace[1], rg.highFace[2], drawHigh);
            emitTri(rg.highFace[3], rg.highFace[4], rg.highFace[5], drawHigh);
            // 側面の三角（隣が非空なら省略＝連続ランプの重なりを排除）
            emitTri(rg.sideA[0], rg.sideA[1], rg.sideA[2],
                    !neighborNonEmpty(chunk, streamer, x, y, z, rg.sideACull));
            emitTri(rg.sideB[0], rg.sideB[1], rg.sideB[2],
                    !neighborNonEmpty(chunk, streamer, x, y, z, rg.sideBCull));
        }
    }

    // ---- GPU アップロード ----
    TerrainMesh& m = chunk.mesh;
    if (m.VAO == 0) glGenVertexArrays(1, &m.VAO);
    if (m.VBO == 0) glGenBuffers(1, &m.VBO);
    if (m.EBO == 0) glGenBuffers(1, &m.EBO);

    glBindVertexArray(m.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(TerrainVertex)),
                 verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, r));

    glBindVertexArray(0);

    m.indexCount = (uint32_t)indices.size();
    m.dirty      = false;

    // ---- CPU 物理キャッシュを保存 ----
    chunk.physVerts        = std::move(physVerts);
    chunk.physIndices      = std::move(physIndices);
    chunk.physConvexBlocks = std::move(physConvexBlocks);
}

// ================================================================== //
//  buildChunkPhysics
// ================================================================== //
#include <include/Core/Physics.hpp>

void buildChunkPhysics(Chunk& chunk, Physics& physics, Instance* userData)
{
    PhysicsTerrainDescriptor descriptor;
    descriptor.origin = Vector3(
        static_cast<float>(chunk.worldOriginX()) * TerrainStreamer::BLOCK_STUD_SIZE,
        static_cast<float>(chunk.worldOriginY()) * TerrainStreamer::BLOCK_STUD_SIZE,
        static_cast<float>(chunk.worldOriginZ()) * TerrainStreamer::BLOCK_STUD_SIZE);
    descriptor.vertices = chunk.physVerts;
    descriptor.indices = chunk.physIndices;
    descriptor.userData = userData;

    static constexpr float BHS = TerrainStreamer::BLOCK_STUD_SIZE * 0.5f;
    descriptor.hulls.reserve(chunk.physConvexBlocks.size());
    for (const ConvexBlock& cb : chunk.physConvexBlocks) {
        int vertexIndices[8];
        const int vertexCount =
            getConvexVertexIndices(static_cast<BlockShape>(cb.shape), vertexIndices);
        if (vertexCount < 4) continue;

        PhysicsTerrainHullDescriptor hull;
        hull.localFrame = CFrame(cb.localCenter);
        hull.vertices.reserve(vertexCount);
        for (int i = 0; i < vertexCount; ++i) {
            const float* localVertex = BASE_VERTS[vertexIndices[i]];
            hull.vertices.emplace_back(
                localVertex[0] * BHS,
                localVertex[1] * BHS,
                localVertex[2] * BHS);
        }
        descriptor.hulls.push_back(std::move(hull));
    }

    chunk.physicsHandle = physics.replaceTerrain(chunk.physicsHandle, descriptor);
}

// ================================================================== //
//  Terrain Instance
// ================================================================== //

Terrain::Terrain() : Instance("Terrain") {}
Terrain::~Terrain() { releaseStreamer(); }

std::string Terrain::getClassName() { return "Terrain"; }

bool Terrain::IsA(std::string className) {
    if (className == "Terrain") return true;
    return Instance::IsA(className);
}

void Terrain::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Enabled") {
        setEnabled(value.as<bool>());
    } else if (name == "DataPath") {
        setDataPath(value.as<std::string>());
    } else if (name == "Seed") {
        Seed = value.as<int>();
    } else if (name == "Flat") {
        Flat = value.as<bool>();
    } else {
        Instance::setProperty(name, value);
    }
}

void Terrain::setEnabled(bool enabled) {
    if (Enabled == enabled) return;
    Enabled = enabled;
    if (!Enabled) releaseStreamer();
}

void Terrain::setDataPath(const std::string& path) {
    if (DataPath == path) return;
    // setDataPath() は旧リージョンをflushしてから切り替える。
    // 空パスはTerrain未構成を表すためstreamerも解除する。
    if (streamer && !path.empty()) {
        streamer->setDataPath(path);
        m_appliedDataPath = path;
    } else if (path.empty()) {
        releaseStreamer();
    }
    DataPath = path;
}

void Terrain::releaseStreamer() {
    if (streamer) {
        streamer->clear();
        streamer.reset();
    }
    m_appliedDataPath.clear();
}

void Terrain::onAncestorChanged() {
    auto* workspace = static_cast<Workspace*>(findFirstAncestorWorkspace());
    if (workspace != m_workspaceOwner) {
        // old Physics が生きている間にchunk handleを破棄する。
        releaseStreamer();
        m_workspaceOwner = workspace;
    }
    Instance::onAncestorChanged();
}

std::shared_ptr<Instance> Terrain::clone() const {
    auto result = std::make_shared<Terrain>();
    result->Name = Name;
    result->Enabled = Enabled;
    result->DataPath = DataPath;
    result->Seed = Seed;
    result->Flat = Flat;
    for (const auto& [name, child] : children) {
        (void)name;
        result->addChild(child->clone());
    }
    return result;
}

void Terrain::update(const Vector3& centerPos) {
    if (!Enabled || DataPath.empty()) {
        releaseStreamer();
        return;
    }

    auto* workspace = static_cast<Workspace*>(findFirstAncestorWorkspace());
    if (!workspace) {
        releaseStreamer();
        m_workspaceOwner = nullptr;
        return;
    }
    if (workspace != m_workspaceOwner) {
        releaseStreamer();
        m_workspaceOwner = workspace;
    }
    if (!streamer) {
        streamer = std::make_unique<TerrainStreamer>(workspace, this, DataPath,
                                                     static_cast<uint32_t>(Seed), Flat);
        m_appliedDataPath = DataPath;
    }

    // DataPath が変更されたら、旧パスへ保存してから新パスへ切り替えて再ロードさせる
    if (DataPath != m_appliedDataPath) {
        streamer->setDataPath(DataPath);
        m_appliedDataPath = DataPath;
    }

    streamer->update(centerPos);
}
