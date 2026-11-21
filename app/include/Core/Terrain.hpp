#pragma once
#include <include/GL/glew.h>
#include <include/Core/PhysicsTypes.hpp>
#include <include/Math/Vector3.hpp>
#include <cstdint>
#include <vector>

// ------------------------------------------------------------------ //
//  Block
// ------------------------------------------------------------------ //

enum class BlockShape : uint8_t {
    Empty,
    Cube,
    // Wedge（上辺の1頂点を削る）4種
    Wedge_TopNE, Wedge_TopNW, Wedge_TopSE, Wedge_TopSW,
    // Wedge（下辺の2頂点を削る）4種
    Wedge_BotNE, Wedge_BotNW, Wedge_BotSE, Wedge_BotSW,
    // Tetra（上辺の3頂点を削る）4種
    Tetra_TopNE, Tetra_TopNW, Tetra_TopSE, Tetra_TopSW,
    // Tetra（下辺の3頂点を削る）4種
    Tetra_BotNE, Tetra_BotNW, Tetra_BotSE, Tetra_BotSW,
    // 三角柱（エッジランプ）: 1辺が1段低いときの斜面。N/S/E/W は斜面が下る方向（低い側）。
    Ramp_N, Ramp_S, Ramp_E, Ramp_W,
};

enum class BlockMaterial : uint8_t {
    Grass,
    Dirt,
    Stone,
};

struct Block {
    BlockShape    shape    = BlockShape::Empty;
    BlockMaterial material = BlockMaterial::Stone;
    uint8_t r = 0, g = 0, b = 0;
    // 計 5 byte/ブロック

    bool isEmpty() const { return shape == BlockShape::Empty; }
};

// ------------------------------------------------------------------ //
//  Mesh
// ------------------------------------------------------------------ //

struct TerrainVertex {
    float x, y, z;    // 位置
    float nx, ny, nz; // 法線
    float u, v;       // UV
    uint8_t r, g, b, a; // 頂点カラー (計 36 byte)
};

struct TerrainMesh {
    GLuint   VAO        = 0;
    GLuint   VBO        = 0;
    GLuint   EBO        = 0;
    uint32_t indexCount = 0;
    bool     dirty      = true;
};

// ------------------------------------------------------------------ //
//  Chunk
// ------------------------------------------------------------------ //

static constexpr int CHUNK_SIZE = 16;

// Cube 以外のブロックは凸包で当たり判定を作る。中心はチャンク原点基準。
struct ConvexBlock {
    uint8_t shape;
    Vector3 localCenter;
};

struct Chunk {
    Block   blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]; // [x][y][z]  約20KB
    int32_t cx = 0, cy = 0, cz = 0;
    TerrainMesh mesh;
    PhysicsTerrainHandle physicsHandle;

    // CPU 側の物理メッシュキャッシュ
    // buildChunkMesh() が描画頂点と同時に埋め、buildChunkPhysics() が参照する。
    std::vector<Vector3>       physVerts;
    std::vector<uint32_t>      physIndices;
    std::vector<ConvexBlock>   physConvexBlocks;

    int32_t worldOriginX() const { return cx * CHUNK_SIZE; }
    int32_t worldOriginY() const { return cy * CHUNK_SIZE; }
    int32_t worldOriginZ() const { return cz * CHUNK_SIZE; }
};

// ------------------------------------------------------------------ //
//  前方宣言
// ------------------------------------------------------------------ //
class Physics;
class TerrainStreamer;
class Instance;
class Workspace;

// チャンクの描画メッシュを（再）生成して VAO/VBO/EBO をアップロードする。
// 同時に physVerts / physIndices を埋める。
// OpenGL コンテキストが有効なスレッドから呼ぶこと。
void buildChunkMesh(Chunk& chunk, const TerrainStreamer* streamer = nullptr);

// チャンクの物理形状を（再）生成する。
// buildChunkMesh() の後に呼ぶこと（physVerts/physIndices を参照するため）。
void buildChunkPhysics(Chunk& chunk, Physics& physics, Instance* userData = nullptr);

#include <Instances/Instance.hpp>
#include <memory>

class Terrain : public Instance {
public:
    bool Enabled = true;
    // 地形データ（リージョンファイル）の保存先ディレクトリ。シーンごとに切り替えられる。
    std::string DataPath;
    int  Seed = 12345; // ノイズ生成のシード
    bool Flat = false; // true なら平坦地形を生成
    std::unique_ptr<TerrainStreamer> streamer;

    Terrain();
    virtual ~Terrain();

    std::string getClassName() override;
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    void onAncestorChanged() override;
    std::shared_ptr<Instance> clone() const override;

    void setEnabled(bool enabled);
    void setDataPath(const std::string& path);
    void releaseStreamer();
    void update(const Vector3& centerPos);

private:
    // 直近 streamer に適用した DataPath。変更検出に使う。
    std::string m_appliedDataPath;
    Workspace* m_workspaceOwner = nullptr;
};
