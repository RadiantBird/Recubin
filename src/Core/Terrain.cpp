#include <include/Core/Terrain.hpp>
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
    5,3,6, 3,2,6,
    0,1,5, 0,5,4,
    0,4,6, 0,6,2,
    1,3,5,
    2,6,3,
};
static const int TRI_WEDGE_TopNW[] = {
    0,3,1, 0,2,3,
    5,7,6,
    0,6,4,
    0,5,1,
    0,6,5,
    0,1,5,
    1,3,7, 1,7,5,
    2,7,3, 2,6,7,
    0,6,2,
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
    0,3,1, 0,2,3,
    4,5,7,
    0,1,5, 0,5,4,
    1,3,7, 1,7,5,
    2,7,3,
    0,4,2,
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

static bool shouldSkipFace(const Chunk& chunk, int x, int y, int z, int face)
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
    if (nx < 0 || nx >= CHUNK_SIZE ||
        ny < 0 || ny >= CHUNK_SIZE ||
        nz < 0 || nz >= CHUNK_SIZE)
        return false;
    return chunk.blocks[nx][ny][nz].shape == BlockShape::Cube;
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
                       float r,  float g,  float b)
{
    verts.push_back({wx,wy,wz, nx,ny,nz, u,v, r,g,b});
}

} // namespace

// ================================================================== //
//  buildChunkMesh
//  描画用 VAO/VBO/EBO を生成し、同時に physVerts / physIndices を埋める。
// ================================================================== //
void buildChunkMesh(Chunk& chunk)
{
    std::vector<TerrainVertex>    verts;
    std::vector<uint32_t>         indices;
    std::vector<physx::PxVec3>    physVerts;
    std::vector<uint32_t>         physIndices;

    verts.reserve(4096);
    indices.reserve(8192);
    physVerts.reserve(4096);
    physIndices.reserve(8192);

    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int y = 0; y < CHUNK_SIZE; y++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    {
        const Block& blk = chunk.blocks[x][y][z];
        if (blk.isEmpty()) continue;

        const float fr = blk.r / 255.0f;
        const float fg = blk.g / 255.0f;
        const float fb = blk.b / 255.0f;

        static constexpr float BS  = 4.0f;  // 1ブロックのサイズ (studs)
        static constexpr float BHS = BS * 0.5f; // ブロック半サイズ

        const float ox = (float)(chunk.worldOriginX() + x) * BS;
        const float oy = (float)(chunk.worldOriginY() + y) * BS;
        const float oz = (float)(chunk.worldOriginZ() + z) * BS;

        const uint8_t shapeIdx = static_cast<uint8_t>(blk.shape);

        if (blk.shape == BlockShape::Cube)
        {
            for (int f = 0; f < 6; f++)
            {
                if (shouldSkipFace(chunk, x, y, z, f)) continue;

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
                    physVerts.push_back({wx, wy, wz});
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
                const uint32_t physBase = (uint32_t)physVerts.size();

                pushVertex(verts, wa[0],wa[1],wa[2], nx,ny,nz, 0.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wb[0],wb[1],wb[2], nx,ny,nz, 1.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wc[0],wc[1],wc[2], nx,ny,nz, 0.5f,1.0f, fr,fg,fb);

                physVerts.push_back({wa[0], wa[1], wa[2]});
                physVerts.push_back({wb[0], wb[1], wb[2]});
                physVerts.push_back({wc[0], wc[1], wc[2]});

                indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
                physIndices.push_back(physBase+0); physIndices.push_back(physBase+1); physIndices.push_back(physBase+2);
            }
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
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, r));

    glBindVertexArray(0);

    m.indexCount = (uint32_t)indices.size();
    m.dirty      = false;

    // ---- CPU 物理キャッシュを保存 ----
    chunk.physVerts   = std::move(physVerts);
    chunk.physIndices = std::move(physIndices);
}

// ================================================================== //
//  buildChunkPhysics
// ================================================================== //
#include <include/Core/Physics.hpp>
#include <include/PhysX/cooking/PxCooking.h>

void buildChunkPhysics(Chunk& chunk, Physics& physics)
{
    // 既存アクターを破棄
    if (chunk.physicsActor) {
        physics.getScene()->removeActor(*chunk.physicsActor);
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }

    if (chunk.physVerts.empty() || chunk.physIndices.empty()) return;

    physx::PxCookingParams cookParams(Physics::GetPhysics()->getTolerancesScale());

    physx::PxTriangleMeshDesc desc;
    desc.points.data     = chunk.physVerts.data();
    desc.points.count    = static_cast<physx::PxU32>(chunk.physVerts.size());
    desc.points.stride   = sizeof(physx::PxVec3);
    desc.triangles.data  = chunk.physIndices.data();
    desc.triangles.count = static_cast<physx::PxU32>(chunk.physIndices.size() / 3);
    desc.triangles.stride = sizeof(uint32_t) * 3;

    physx::PxDefaultMemoryOutputStream buf;
    if (!PxCookTriangleMesh(cookParams, desc, buf)) return;

    physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
    physx::PxTriangleMesh* triMesh = Physics::GetPhysics()->createTriangleMesh(input);
    if (!triMesh) return;

    physx::PxMaterial* mat = Physics::GetPhysics()->createMaterial(0.5f, 0.5f, 0.2f);
    physx::PxRigidStatic* actor = Physics::GetPhysics()->createRigidStatic(
        physx::PxTransform(physx::PxIdentity)
    );
    physx::PxTriangleMeshGeometry geom(triMesh);
    geom.meshFlags = physx::PxMeshGeometryFlag::eDOUBLE_SIDED;
    physx::PxRigidActorExt::createExclusiveShape(
        *actor,
        geom,
        *mat
    );
    physics.getScene()->addActor(*actor);

    triMesh->release();
    mat->release();

    chunk.physicsActor = actor;
}