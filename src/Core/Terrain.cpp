#include <include/Core/Terrain.hpp>
#include <vector>
#include <cstring>

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

// ------------------------------------------------------------------ //
//  8頂点のローカル位置（半ブロック単位 → 実スケールはcaller側でオフセット）
// ------------------------------------------------------------------ //
static const float BASE_VERTS[8][3] = {
    {-1,-1,-1}, {+1,-1,-1},   // 0,1
    {-1,-1,+1}, {+1,-1,+1},   // 2,3
    {-1,+1,-1}, {+1,+1,-1},   // 4,5
    {-1,+1,+1}, {+1,+1,+1},   // 6,7
};

// ------------------------------------------------------------------ //
//  面定義（各面 = 4頂点インデックス + 法線）
//  三角形は 0,1,2  0,2,3 の順（反時計回り=表）
// ------------------------------------------------------------------ //
struct FaceDef {
    int   vi[4];      // BASE_VERTS インデックス
    float nx, ny, nz; // 面法線
};

// Cube の6面
static const FaceDef CUBE_FACES[6] = {
    {{4,5,7,6},  0, 1, 0},  // Top    (+Y)
    {{0,2,3,1},  0,-1, 0},  // Bottom (-Y)
    {{2,6,7,3},  0, 0, 1},  // South  (+Z)
    {{0,1,5,4},  0, 0,-1},  // North  (-Z)
    {{1,3,7,5},  1, 0, 0},  // East   (+X)
    {{0,4,6,2}, -1, 0, 0},  // West   (-X)
};

// 面インデックス定数（隠れ面判定で使う）
static constexpr int FACE_TOP    = 0;
static constexpr int FACE_BOTTOM = 1;
static constexpr int FACE_SOUTH  = 2;
static constexpr int FACE_NORTH  = 3;
static constexpr int FACE_EAST   = 4;
static constexpr int FACE_WEST   = 5;

// ------------------------------------------------------------------ //
//  形状ごとの三角形テーブル
//  各エントリ = BASE_VERTS インデックスの三角形リスト（3の倍数）
//  法線は三角形から都度計算する（非平面面があるため）
// ------------------------------------------------------------------ //
struct ShapeTri {
    const int*  idx;
    int         count; // 頂点数（3の倍数）
};

// --- Cube ---
static const int TRI_CUBE[] = {
    // Top
    4,5,7, 4,7,6,
    // Bottom
    0,3,1, 0,2,3,
    // South (+Z)
    2,7,3, 2,6,7,
    // North (-Z)
    0,1,5, 0,5,4,
    // East (+X)
    1,3,7, 1,7,5,
    // West (-X)
    0,6,2, 0,4,6,
};

// --- Wedge_TopNE  省く頂点:7 ---
// 上面は三角形(4,5,6)、斜面は(5,7相当を6で代替)... 実際には斜面(5,6,3),(3,6,2は下面)
// 使用頂点: 0,1,2,3,4,5,6
static const int TRI_WEDGE_TopNE[] = {
    // Bottom
    0,3,1, 0,2,3,
    // Top(三角形)
    4,5,6,
    // Slope(7省略、5→6で斜面)
    5,3,6, 3,2,6,
    // North (-Z): 0,1,5,4
    0,1,5, 0,5,4,
    // West (-X): 0,4,6,2
    0,4,6, 0,6,2,
    // East(+X): 1,3,5 (7省くので三角形)
    1,3,5,
    // South(+Z): 2,6,3 (7省くので三角形)
    2,6,3,
};

// --- Wedge_TopNW  省く頂点:4 ---
// 使用頂点: 0,1,2,3,5,6,7
static const int TRI_WEDGE_TopNW[] = {
    // Bottom
    0,3,1, 0,2,3,
    // Top(三角形)
    5,7,6,
    // Slope
    0,6,4, // ← 4省くので0→6
    0,5,1, // North
    0,6,5,
    // North (-Z)
    0,1,5,
    // East (+X)
    1,3,7, 1,7,5,
    // South (+Z)
    2,7,3, 2,6,7,
    // West(-X): 0,6,2 (4省くので三角形)
    0,6,2,
};

// --- Wedge_TopSE  省く頂点:5 ---
// 使用頂点: 0,1,2,3,4,6,7
static const int TRI_WEDGE_TopSE[] = {
    // Bottom
    0,3,1, 0,2,3,
    // Top(三角形)
    4,7,6,
    // North (-Z): 0,1→4(5省く)
    0,1,4,
    // East(+X): 1,3,7 → 4(5省く)
    1,3,7, 1,7,4,
    // South(+Z)
    2,7,3, 2,6,7,
    // West(-X)
    0,4,6, 0,6,2,
};

// --- Wedge_TopSW  省く頂点:6 ---
// 使用頂点: 0,1,2,3,4,5,7
static const int TRI_WEDGE_TopSW[] = {
    // Bottom
    0,3,1, 0,2,3,
    // Top(三角形)
    4,5,7,
    // North (-Z)
    0,1,5, 0,5,4,
    // East (+X)
    1,3,7, 1,7,5,
    // South(+Z): 2,7,3(6省く→7で代替斜面)
    2,7,3,
    // West(-X): 0,4,2(6省く)
    0,4,2,
};

// --- Wedge_BotNE  省く頂点:1,3 ---
// 使用頂点: 0,2,4,5,6,7
static const int TRI_WEDGE_BotNE[] = {
    // Top
    4,5,7, 4,7,6,
    // Bottom(三角形、1,3省く)
    0,2,4, // Slope
    // Slope(東面消滅、斜面)
    0,4,5, // North slope
    2,6,0, 0,6,4, // West
    5,7,6, // already top
    // South(+Z)
    2,6,7,
    // North(-Z)
    0,5,4,
    // West(-X)
    0,2,6, 0,6,4,
};

// --- Wedge_BotNW  省く頂点:0,2 ---
// 使用頂点: 1,3,4,5,6,7
static const int TRI_WEDGE_BotNW[] = {
    // Top
    4,5,7, 4,7,6,
    // Slope(西面消滅)
    1,5,4, // North slope
    // South(+Z)
    6,7,3,
    // North(-Z)
    1,5,4,
    // East(+X)
    1,3,7, 1,7,5,
    // Slope bottom
    1,4,6, 1,6,3,
};

// --- Wedge_BotSE  省く頂点:2,4 ---
// 使用頂点: 0,1,3,5,6,7
static const int TRI_WEDGE_BotSE[] = {
    // Top
    5,7,6,  // 4省くので三角形
    // East(+X)
    1,3,7, 1,7,5,
    // North(-Z)
    0,1,5,
    // South slope
    6,7,3, 6,3,0,
    // Slope bottom
    0,5,6,
};

// --- Wedge_BotSW  省く頂点:3,5 ---
// 使用頂点: 0,1,2,4,6,7
static const int TRI_WEDGE_BotSW[] = {
    // Top
    4,7,6, // 5省くので三角形
    // West(-X)
    0,4,6, 0,6,2,
    // North(-Z)
    0,1,4,
    // South(+Z)
    2,6,7,
    // Slope bottom
    1,4,0, 1,7,4,
};

// --- Tetra_TopNE  使用頂点: 0,1,2,3,7 ---
static const int TRI_TETRA_TopNE[] = {
    // Bottom
    0,3,1, 0,2,3,
    // East(+X)
    1,3,7,
    // South(+Z)
    2,7,3,
    // Slope(斜面: 0,1,7,2)
    0,1,7, 0,7,2,
};

// --- Tetra_TopNW  使用頂点: 0,1,2,3,4 ---
static const int TRI_TETRA_TopNW[] = {
    // Bottom
    0,3,1, 0,2,3,
    // North(-Z)
    0,1,4,
    // West(-X)
    0,4,2,
    // Slope
    1,4,0, 1,3,4, // ← 4はapex
    // Slope2
    2,4,3,
};

// --- Tetra_TopSE  使用頂点: 0,1,2,3,5 ---
static const int TRI_TETRA_TopSE[] = {
    // Bottom
    0,3,1, 0,2,3,
    // North(-Z)
    0,1,5, 0,5,4, // 4省く→簡略
    // East(+X)
    1,3,5,
    // Slope
    0,5,1, 2,3,5,
    0,2,5,
};

// --- Tetra_TopSW  使用頂点: 0,1,2,3,6 ---
static const int TRI_TETRA_TopSW[] = {
    // Bottom
    0,3,1, 0,2,3,
    // South(+Z)
    2,6,3,
    // West(-X)
    0,6,2,
    // Slope
    0,1,6, 1,3,6,
};

// --- Tetra_BotNE  使用頂点: 1,4,5,6,7 ---
static const int TRI_TETRA_BotNE[] = {
    // Top
    4,5,7, 4,7,6,
    // East(+X)
    1,7,5,
    // Slope
    1,5,4, 1,4,6, 1,6,7,
};

// --- Tetra_BotNW  使用頂点: 0,4,5,6,7 ---
static const int TRI_TETRA_BotNW[] = {
    // Top
    4,5,7, 4,7,6,
    // West(-X)
    0,4,6,
    // Slope
    0,6,7, 0,7,5, 0,5,4,
};

// --- Tetra_BotSE  使用頂点: 3,4,5,6,7 ---
static const int TRI_TETRA_BotSE[] = {
    // Top
    4,5,7, 4,7,6,
    // East(+X)
    3,7,5,
    // South(+Z)
    3,6,7,
    // Slope
    3,5,4, 3,4,6,
};

// --- Tetra_BotSW  使用頂点: 2,4,5,6,7 ---
static const int TRI_TETRA_BotSW[] = {
    // Top
    4,5,7, 4,7,6,
    // South(+Z)
    2,6,7,
    // West(-X)
    2,4,6,
    // Slope
    2,7,4, 2,5,7, // 
    2,4,5,
};

// 形状→三角形テーブルのマップ
static const int* SHAPE_TRIS[] = {
    nullptr,       // Empty
    TRI_CUBE,      // Cube
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

// ------------------------------------------------------------------ //
//  隣接チェック（Cube同士のみ隠れ面除去、それ以外は常に面を出す）
// ------------------------------------------------------------------ //
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

    // チャンク境界外 → 常に面を出す（将来隣チャンク参照に拡張）
    if (nx < 0 || nx >= CHUNK_SIZE ||
        ny < 0 || ny >= CHUNK_SIZE ||
        nz < 0 || nz >= CHUNK_SIZE)
        return false;

    const Block& nb = chunk.blocks[nx][ny][nz];
    // 隣が Cube のときだけスキップ（Wedge/Tetra 隣接は常に面を出す）
    return nb.shape == BlockShape::Cube;
}

// ------------------------------------------------------------------ //
//  法線計算ヘルパー
// ------------------------------------------------------------------ //
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

// ------------------------------------------------------------------ //
//  頂点を追加するヘルパー
// ------------------------------------------------------------------ //
static void pushVertex(std::vector<TerrainVertex>& verts,
                       float wx, float wy, float wz,
                       float nx, float ny, float nz,
                       float u,  float v,
                       float r,  float g,  float b)
{
    TerrainVertex vtx;
    vtx.x=wx; vtx.y=wy; vtx.z=wz;
    vtx.nx=nx; vtx.ny=ny; vtx.nz=nz;
    vtx.u=u;  vtx.v=v;
    vtx.r=r;  vtx.g=g;  vtx.b=b;
    verts.push_back(vtx);
}

} // namespace

// ================================================================== //
//  buildChunkMesh
// ================================================================== //
void buildChunkMesh(Chunk& chunk)
{
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t>      indices;
    verts.reserve(4096);
    indices.reserve(8192);

    const float BLOCK_SCALE = 2.0f; // BASE_VERTS が ±1 なので ÷2 してオフセット
    // 実際のブロックサイズ = 1 ブロック単位（ワールド座標は caller がスケール）
    // ここでは 1ブロック = 1.0f ワールド単位として出力する

    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int y = 0; y < CHUNK_SIZE; y++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    {
        const Block& blk = chunk.blocks[x][y][z];
        if (blk.isEmpty()) continue;

        const float fr = blk.r / 255.0f;
        const float fg = blk.g / 255.0f;
        const float fb = blk.b / 255.0f;

        // ブロック中心（ワールド座標）
        const float ox = (float)(chunk.worldOriginX() + x);
        const float oy = (float)(chunk.worldOriginY() + y);
        const float oz = (float)(chunk.worldOriginZ() + z);

        const uint8_t shapeIdx = static_cast<uint8_t>(blk.shape);

        // --- Cube：隠れ面除去あり ---
        if (blk.shape == BlockShape::Cube)
        {
            for (int f = 0; f < 6; f++)
            {
                if (shouldSkipFace(chunk, x, y, z, f)) continue;

                const FaceDef& fd = CUBE_FACES[f];
                const uint32_t base = (uint32_t)verts.size();

                for (int vi = 0; vi < 4; vi++)
                {
                    const float* lv = BASE_VERTS[fd.vi[vi]];
                    pushVertex(verts,
                        ox + lv[0]*0.5f,
                        oy + lv[1]*0.5f,
                        oz + lv[2]*0.5f,
                        fd.nx, fd.ny, fd.nz,
                        (vi==0||vi==3)?0.0f:1.0f,
                        (vi<2)?0.0f:1.0f,
                        fr, fg, fb);
                }
                // 2つの三角形
                indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
                indices.push_back(base+0); indices.push_back(base+2); indices.push_back(base+3);
            }
        }
        // --- Wedge / Tetra：三角形テーブルから生成、隠れ面除去なし ---
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

                // ワールド座標に変換
                float wa[3] = { ox+va[0]*0.5f, oy+va[1]*0.5f, oz+va[2]*0.5f };
                float wb[3] = { ox+vb[0]*0.5f, oy+vb[1]*0.5f, oz+vb[2]*0.5f };
                float wc[3] = { ox+vc[0]*0.5f, oy+vc[1]*0.5f, oz+vc[2]*0.5f };

                float nx, ny, nz;
                calcNormal(wa, wb, wc, nx, ny, nz);

                const uint32_t base = (uint32_t)verts.size();
                pushVertex(verts, wa[0],wa[1],wa[2], nx,ny,nz, 0.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wb[0],wb[1],wb[2], nx,ny,nz, 1.0f,0.0f, fr,fg,fb);
                pushVertex(verts, wc[0],wc[1],wc[2], nx,ny,nz, 0.5f,1.0f, fr,fg,fb);

                indices.push_back(base+0);
                indices.push_back(base+1);
                indices.push_back(base+2);
            }
        }
    }

    // ---------------------------------------------------------------- //
    //  GPU アップロード
    // ---------------------------------------------------------------- //
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

    // 属性レイアウト（TerrainVertex と一致）
    // location 0: position (xyz)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, x));
    // location 1: normal (nxnynz)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, nx));
    // location 2: uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, u));
    // location 3: color (rgb)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          (void*)offsetof(TerrainVertex, r));

    glBindVertexArray(0);

    m.indexCount = (uint32_t)indices.size();
    m.dirty      = false;
}