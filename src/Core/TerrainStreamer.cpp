#include <include/Core/TerrainStreamer.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/FileLoader.hpp>
#include <Core/Physics.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <cmath>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// ================================================================== //
//  内部ユーティリティ
// ================================================================== //
namespace {

void ensureDir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

inline int32_t chunkToRegion(int32_t c) {
    return (c >= 0) ? (c / TerrainStreamer::REGION_SIZE)
                    : ((c - TerrainStreamer::REGION_SIZE + 1) / TerrainStreamer::REGION_SIZE);
}

inline int32_t worldToChunk(float w) {
    int32_t b = static_cast<int32_t>(std::floor(w));
    return (b >= 0) ? (b / CHUNK_SIZE) : ((b - CHUNK_SIZE + 1) / CHUNK_SIZE);
}

struct RleEntry { int count; uint8_t shape, material, r, g, b; };

std::vector<RleEntry> rleEncode(const Chunk& chunk) {
    std::vector<RleEntry> out;
    RleEntry cur{}; cur.count = 0;
    auto flush = [&]() { if (cur.count > 0) out.push_back(cur); };
    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    for (int y = 0; y < CHUNK_SIZE; y++) {
        const Block& b = chunk.blocks[x][y][z];
        uint8_t s = (uint8_t)b.shape, m = (uint8_t)b.material;
        if (cur.count > 0 && cur.shape==s && cur.material==m &&
            cur.r==b.r && cur.g==b.g && cur.b==b.b) {
            cur.count++;
        } else {
            flush();
            cur = { 1, s, m, b.r, b.g, b.b };
        }
    }
    flush();
    return out;
}

void rleDecode(const std::vector<RleEntry>& rle, Chunk& chunk) {
    int idx = 0;
    for (const auto& e : rle) {
        for (int n = 0; n < e.count; n++, idx++) {
            int x = idx / (CHUNK_SIZE * CHUNK_SIZE);
            int z = (idx % (CHUNK_SIZE * CHUNK_SIZE)) / CHUNK_SIZE;
            int y = idx % CHUNK_SIZE;
            if (x >= CHUNK_SIZE) break;
            Block& b = chunk.blocks[x][y][z];
            b.shape    = (BlockShape)e.shape;
            b.material = (BlockMaterial)e.material;
            b.r = e.r; b.g = e.g; b.b = e.b;
        }
    }
}

} // namespace

// ================================================================== //
//  コンストラクタ / デストラクタ
// ================================================================== //
TerrainStreamer::TerrainStreamer(Renderer* renderer, Workspace* workspace)
    : m_renderer(renderer), m_workspace(workspace), m_noise(12345u)
{
    ensureDir(terrainDir);
}

TerrainStreamer::~TerrainStreamer() {
    std::vector<ChunkKey> keys;
    keys.reserve(m_chunks.size());
    for (auto& [k, _] : m_chunks) keys.push_back(k);
    for (auto& k : keys) unloadChunk(k.cx, k.cy, k.cz);
    flushRegions();
}

void TerrainStreamer::setWorkspace(Workspace* workspace) {
    m_workspace = workspace;
}

void TerrainStreamer::clear() {
    std::vector<ChunkKey> keys;
    for (auto& [k, _] : m_chunks) keys.push_back(k);
    for (auto& k : keys) unloadChunk(k.cx, k.cy, k.cz);
    flushRegions();
    m_regions.clear();
}

Physics* TerrainStreamer::getPhysics() const {
    if (!m_workspace) return nullptr;
    return m_workspace->getPhysicsEngine();
}

// ================================================================== //
//  update
// ================================================================== //
void TerrainStreamer::update(const Vector3& playerPos)
{
    const int32_t pcx = worldToChunk(playerPos.x);
    const int32_t pcy = worldToChunk(playerPos.y);
    const int32_t pcz = worldToChunk(playerPos.z);

    // 範囲内チャンクをロード
    for (int dx = -STREAM_RADIUS; dx <= STREAM_RADIUS; dx++)
    for (int cy = 0; cy < 8; cy++) // Y軸は 0～7 を常にロード
    for (int dz = -STREAM_RADIUS; dz <= STREAM_RADIUS; dz++) {
        ChunkKey key{ pcx+dx, cy, pcz+dz };
        if (m_chunks.find(key) == m_chunks.end())
            loadChunk(key.cx, key.cy, key.cz);
    }

    // 範囲外チャンクをアンロード
    std::vector<ChunkKey> toUnload;
    for (auto& [key, _] : m_chunks) {
        if (std::abs(key.cx-pcx) > STREAM_RADIUS ||
            std::abs(key.cz-pcz) > STREAM_RADIUS ||
            key.cy < 0 || key.cy >= 8)
            toUnload.push_back(key);
    }
    for (auto& key : toUnload) unloadChunk(key.cx, key.cy, key.cz);

    // dirty チャンクの再生成
    for (auto& [_, entry] : m_chunks) rebuildIfDirty(entry);
}

// ================================================================== //
//  ロード / アンロード
// ================================================================== //
void TerrainStreamer::loadChunk(int32_t cx, int32_t cy, int32_t cz)
{
    auto& entry  = m_chunks[{cx, cy, cz}];
    Chunk& chunk = entry.chunk;
    chunk.cx = cx; chunk.cy = cy; chunk.cz = cz;
    chunk.mesh.dirty = true;
    entry.modified   = false;

    readChunkFromRegion(chunk);

    if (m_renderer) m_renderer->registerTerrainChunk(&chunk);
}

void TerrainStreamer::unloadChunk(int32_t cx, int32_t cy, int32_t cz)
{
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end()) return;
    Chunk& chunk = it->second.chunk;

    if (it->second.modified) writeChunkToRegion(chunk);

    // 物理アクターを解放
    if (chunk.physicsActor) {
        Physics* phys = getPhysics();
        if (phys && phys->getScene()) {
            phys->getScene()->removeActor(*chunk.physicsActor);
        }
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }

    // GPU バッファを解放
    if (chunk.mesh.VAO) { glDeleteVertexArrays(1, &chunk.mesh.VAO); chunk.mesh.VAO = 0; }
    if (chunk.mesh.VBO) { glDeleteBuffers(1, &chunk.mesh.VBO);      chunk.mesh.VBO = 0; }
    if (chunk.mesh.EBO) { glDeleteBuffers(1, &chunk.mesh.EBO);      chunk.mesh.EBO = 0; }

    if (m_renderer) m_renderer->unregisterTerrainChunk(&chunk);
    m_chunks.erase(it);
}

void TerrainStreamer::rebuildIfDirty(ChunkEntry& entry)
{
    if (!entry.chunk.mesh.dirty) return;
    buildChunkMesh(entry.chunk, this);
    Physics* phys = getPhysics();
    if (phys) buildChunkPhysics(entry.chunk, *phys);
}

// ================================================================== //
//  ファイル I/O
// ================================================================== //
std::string TerrainStreamer::regionPath(const std::string& dir, int32_t rx, int32_t rz) {
    return dir + "/r_" + std::to_string(rx) + "_" + std::to_string(rz) + ".yaml";
}

TerrainStreamer::RegionCache& TerrainStreamer::getRegionCache(int32_t rx, int32_t rz) {
    auto& cache = m_regions[{rx, rz}];
    if (!cache.loaded) {
        std::string path = regionPath(terrainDir, rx, rz);
        std::string content = FileLoader::readText(path);
        if (!content.empty()) {
            try { cache.root = YAML::Load(content); } catch (...) {}
        }
        if (!cache.root["chunks"] || !cache.root["chunks"].IsSequence()) {
            cache.root["chunks"] = YAML::Node(YAML::NodeType::Sequence);
        }
        cache.loaded = true;
        cache.modified = false;
    }
    return cache;
}

void TerrainStreamer::flushRegions() {
    ensureDir(terrainDir);
    for (auto& [k, cache] : m_regions) {
        if (cache.loaded && cache.modified) {
            std::string path = regionPath(terrainDir, k.rx, k.rz);
            YAML::Emitter out;
            out << cache.root;
            std::ofstream ofs(path);
            if (ofs.is_open()) ofs << out.c_str();
            else std::cerr << "[TerrainStreamer] Failed to write: " << path << std::endl;
            cache.modified = false;
        }
    }
}

void TerrainStreamer::readChunkFromRegion(Chunk& chunk)
{
    int32_t rx = chunkToRegion(chunk.cx);
    int32_t rz = chunkToRegion(chunk.cz);
    RegionCache& cache = getRegionCache(rx, rz);

    try {
        YAML::Node chunks = cache.root["chunks"];
        for (const auto& node : chunks) {
            if (node["cx"].as<int32_t>() != chunk.cx) continue;
            if (node["cy"].as<int32_t>() != chunk.cy) continue;
            if (node["cz"].as<int32_t>() != chunk.cz) continue;
            std::vector<RleEntry> rle;
            for (const auto& e : node["blocks"])
                rle.push_back({ e[0].as<int>(), e[1].as<uint8_t>(), e[2].as<uint8_t>(),
                                 e[3].as<uint8_t>(), e[4].as<uint8_t>(), e[5].as<uint8_t>() });
            rleDecode(rle, chunk);
            return;
        }
        generateChunk(chunk); // このチャンクのデータなし

    } catch (const std::exception& e) {
        std::cerr << "[TerrainStreamer] YAML parse error: " << e.what() << std::endl;
        generateChunk(chunk);
    }
}

void TerrainStreamer::writeChunkToRegion(const Chunk& chunk)
{
    int32_t rx = chunkToRegion(chunk.cx);
    int32_t rz = chunkToRegion(chunk.cz);
    RegionCache& cache = getRegionCache(rx, rz);

    YAML::Node& root = cache.root;

    // 既存エントリを除いた新シーケンスを構築
    YAML::Node newChunks(YAML::NodeType::Sequence);
    for (const auto& node : root["chunks"]) {
        bool match = node["cx"].as<int32_t>()==chunk.cx &&
                     node["cy"].as<int32_t>()==chunk.cy &&
                     node["cz"].as<int32_t>()==chunk.cz;
        if (!match) newChunks.push_back(node);
    }

    YAML::Node chunkNode;
    chunkNode["cx"] = chunk.cx;
    chunkNode["cy"] = chunk.cy;
    chunkNode["cz"] = chunk.cz;
    YAML::Node blocksNode(YAML::NodeType::Sequence);
    for (const auto& e : rleEncode(chunk)) {
        YAML::Node row(YAML::NodeType::Sequence);
        row.push_back(e.count); row.push_back((int)e.shape);
        row.push_back((int)e.material);
        row.push_back((int)e.r); row.push_back((int)e.g); row.push_back((int)e.b);
        blocksNode.push_back(row);
    }
    chunkNode["blocks"] = blocksNode;
    newChunks.push_back(chunkNode);
    root["chunks"] = newChunks;

    cache.modified = true;
}

// ================================================================== //
//  プロシージャル生成（平坦テスト。ノイズ実装済み）
// ================================================================== //
static constexpr float  TERRAIN_SCALE      = 0.003f; // ノイズのXZ周波数（小さいほど広い山）
static constexpr int    TERRAIN_OCTAVES    = 6;       // fBm オクターブ数
static constexpr float  TERRAIN_PERSIST    = 0.5f;   // 各オクターブの振幅減衰
static constexpr float  TERRAIN_LACUNARITY = 2.0f;   // 各オクターブの周波数倍率
static constexpr float  TERRAIN_HEIGHT_MAX = 64.0f;  // 最大地形高さ（ブロック）
static constexpr float  TERRAIN_HEIGHT_MID = 8.0f;   // 海面相当のY座標（ブロック）
static constexpr int    DIRT_DEPTH         = 3;       // 草の下に何ブロック Dirt を置くか

void TerrainStreamer::generateChunk(Chunk& chunk)
{
    const int worldX0 = chunk.worldOriginX();
    const int worldY0 = chunk.worldOriginY();
    const int worldZ0 = chunk.worldOriginZ();
 
    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    {
        // このXZ列の地表高さを fBm で決定
        float wx = static_cast<float>(worldX0 + x);
        float wz = static_cast<float>(worldZ0 + z);
 
        float n = m_noise.fbm2(wx * TERRAIN_SCALE,
                               wz * TERRAIN_SCALE,
                               TERRAIN_OCTAVES,
                               TERRAIN_PERSIST,
                               TERRAIN_LACUNARITY);
        // n は [-1, 1] → ワールドY高さに変換
        int surfaceY = static_cast<int>(TERRAIN_HEIGHT_MID + n * TERRAIN_HEIGHT_MAX);
 
        for (int y = 0; y < CHUNK_SIZE; y++)
        {
            int wy = worldY0 + y;
            Block& b = chunk.blocks[x][y][z];
 
            if (wy > surfaceY) {
                // 地表より上: Empty
                b.shape = BlockShape::Empty;
 
            } else if (wy == surfaceY) {
                // 地表: Grass
                b.shape    = BlockShape::Cube;
                b.material = BlockMaterial::Grass;
                b.r = 60; b.g = 140; b.b = 40;
 
            } else if (wy >= surfaceY - DIRT_DEPTH) {
                // 地表直下: Dirt
                b.shape    = BlockShape::Cube;
                b.material = BlockMaterial::Dirt;
                b.r = 139; b.g = 90; b.b = 43;
 
            } else {
                // 深部: Stone（深さに応じて少し暗くする）
                b.shape    = BlockShape::Cube;
                b.material = BlockMaterial::Stone;
                int depth  = surfaceY - wy;
                int shadeInt = (std::max)(60, 128 - depth);
                uint8_t shade = static_cast<uint8_t>(shadeInt);
                b.r = shade; b.g = shade; b.b = shade;
            }
        }
    }
}

// ================================================================== //
//  ユーティリティ
// ================================================================== //
Chunk* TerrainStreamer::getChunk(int32_t cx, int32_t cy, int32_t cz) {
    auto it = m_chunks.find({cx, cy, cz});
    return (it != m_chunks.end()) ? &it->second.chunk : nullptr;
}

const Block* TerrainStreamer::getBlockGlobal(int32_t wx, int32_t wy, int32_t wz) const {
    int32_t cx = (wx >= 0) ? (wx / CHUNK_SIZE) : ((wx - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cy = (wy >= 0) ? (wy / CHUNK_SIZE) : ((wy - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cz = (wz >= 0) ? (wz / CHUNK_SIZE) : ((wz - CHUNK_SIZE + 1) / CHUNK_SIZE);
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end()) return nullptr;

    int bx = wx - cx * CHUNK_SIZE;
    int by = wy - cy * CHUNK_SIZE;
    int bz = wz - cz * CHUNK_SIZE;
    return &it->second.chunk.blocks[bx][by][bz];
}

void TerrainStreamer::markDirty(int32_t cx, int32_t cy, int32_t cz) {
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end()) return;
    it->second.chunk.mesh.dirty = true;
    it->second.modified = true;
}