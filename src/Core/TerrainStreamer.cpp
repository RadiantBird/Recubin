#include <include/Core/TerrainStreamer.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/FileLoader.hpp>
#include <Core/Physics.hpp>
#include <include/Instances/PathfindingService.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows26.h>
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

// ワールド座標（studs）→チャンク座標。
// 1ブロック = BLOCK_STUD_SIZE studs なので、まずブロック座標に変換してからチャンク化する。
inline int32_t worldToChunk(float studs) {
    int32_t b = static_cast<int32_t>(std::floor(studs / TerrainStreamer::BLOCK_STUD_SIZE));
    return (b >= 0) ? (b / CHUNK_SIZE) : ((b - CHUNK_SIZE + 1) / CHUNK_SIZE);
}

// ワールド「ブロック座標」→チャンク座標（負数で正しく floor 方向へ丸める）。
inline int32_t blockToChunk(int32_t b) {
    return (b >= 0) ? (b / CHUNK_SIZE) : ((b - CHUNK_SIZE + 1) / CHUNK_SIZE);
}

// (wx,wy,wz) を1つのキーへパックする（Diffキャプチャの重複検出用）。
inline uint64_t packVoxelKey(int32_t wx, int32_t wy, int32_t wz) {
    return (uint64_t)(uint32_t)wx << 42 | (uint64_t)(uint32_t)wy << 21 | (uint64_t)(uint32_t)wz;
}

// axis(0=X,1=Y,2=Z) / 残り2軸の座標(a,b) / axis座標(coord) からワールドブロック座標へ変換する。
// axis==0: (wx=coord,wy=a,wz=b) / axis==1: (wx=a,wy=coord,wz=b) / axis==2: (wx=a,wy=b,wz=coord)
inline void axisToWorld(int32_t axis, int32_t a, int32_t b, int32_t coord,
                         int32_t& wx, int32_t& wy, int32_t& wz) {
    if (axis == 0)      { wx = coord; wy = a; wz = b; }
    else if (axis == 1) { wx = a; wy = coord; wz = b; }
    else                { wx = a; wy = b; wz = coord; }
}

// Vector3の指定軸成分を取り出す。
inline float axisComponent(const Vector3& v, int32_t axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

struct RleEntry { int count; uint8_t shape, material, r, g, b; };

using BlockArray = Block[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];

std::vector<RleEntry> rleEncode(const BlockArray& blocks) {
    std::vector<RleEntry> out;
    RleEntry cur{}; cur.count = 0;
    auto flush = [&]() { if (cur.count > 0) out.push_back(cur); };
    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    for (int y = 0; y < CHUNK_SIZE; y++) {
        const Block& b = blocks[x][y][z];
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

void rleDecode(const std::vector<RleEntry>& rle, BlockArray& blocks) {
    int idx = 0;
    for (const auto& e : rle) {
        for (int n = 0; n < e.count; n++, idx++) {
            int x = idx / (CHUNK_SIZE * CHUNK_SIZE);
            int z = (idx % (CHUNK_SIZE * CHUNK_SIZE)) / CHUNK_SIZE;
            int y = idx % CHUNK_SIZE;
            if (x >= CHUNK_SIZE) break;
            Block& b = blocks[x][y][z];
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
TerrainStreamer::TerrainStreamer(Workspace* workspace, Instance* owner, const std::string& dataDir,
                                 uint32_t seed, bool flat)
    : m_workspace(workspace), m_owner(owner), m_noise(seed), m_flat(flat)
{
    terrainDir = dataDir;
    ensureDir(terrainDir);
    // ワーカースレッド起動（terrainDir 設定後・m_chunks 未使用の時点で開始）
    m_worker = std::thread([this]{ workerLoop(); });
}

TerrainStreamer::~TerrainStreamer() {
    // 未処理の Load ジョブは破棄して join を高速化（シーン切替時のフリーズ防止）。
    // Save ジョブは編集の永続化に必要なので残す。
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        std::deque<Job> keep;
        for (auto& j : m_jobQueue) if (j.type != JobType::Load) keep.push_back(std::move(j));
        m_jobQueue.swap(keep);
    }
    // 変更済みチャンクを保存ジョブとして投入してから停止する
    for (auto& [k, entry] : m_chunks) {
        if (entry.state == ChunkState::Ready && entry.modified) {
            Job job; job.type = JobType::Save;
            job.cx = k.cx; job.cy = k.cy; job.cz = k.cz;
            job.grid = std::make_unique<BlockGrid>();
            std::memcpy(job.grid->data, entry.chunk.blocks, sizeof(BlockGrid));
            enqueueJob(std::move(job));
        }
    }
    { Job job; job.type = JobType::Stop; enqueueJob(std::move(job)); } // Stop 内で最終フラッシュ
    if (m_worker.joinable()) m_worker.join();

    // GL/PhysX リソースを解放（メインスレッド）
    for (auto& [k, entry] : m_chunks) releaseChunkResources(entry.chunk);
    m_chunks.clear();
}

void TerrainStreamer::setWorkspace(Workspace* workspace) {
    m_workspace = workspace;
}

void TerrainStreamer::setDataPath(const std::string& dir) {
    // 変更済みを保存投入 → メイン側リソース解放 → SetDir（worker が旧フラッシュ+regionクリア+dir更新）
    for (auto& [k, entry] : m_chunks) {
        if (entry.state == ChunkState::Ready && entry.modified) {
            Job job; job.type = JobType::Save;
            job.cx = k.cx; job.cy = k.cy; job.cz = k.cz;
            job.grid = std::make_unique<BlockGrid>();
            std::memcpy(job.grid->data, entry.chunk.blocks, sizeof(BlockGrid));
            enqueueJob(std::move(job));
        }
        releaseChunkResources(entry.chunk);
    }
    m_chunks.clear();
    { Job job; job.type = JobType::SetDir; job.dir = dir; enqueueJob(std::move(job)); }
    // 結果キューに残る旧チャンクの結果はキー不在で破棄される
}

void TerrainStreamer::regenerate(uint32_t seed, bool flat) {
    // 保存済み地形（編集含む）を破棄して新シード/モードで作り直す。
    // メイン: GL/PhysX を解放し m_chunks をクリア。未処理 Load を破棄（旧シード生成を防ぐ）。
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        std::deque<Job> keep;
        for (auto& j : m_jobQueue) if (j.type != JobType::Load) keep.push_back(std::move(j));
        m_jobQueue.swap(keep);
    }
    for (auto& [k, entry] : m_chunks) releaseChunkResources(entry.chunk);
    m_chunks.clear();
    // ワーカーで reseed + region クリア + DataPath 削除（単一ワーカーなので m_noise 競合なし）
    Job job; job.type = JobType::Regenerate; job.seed = seed; job.flat = flat;
    enqueueJob(std::move(job));
}

void TerrainStreamer::clear() {
    for (auto& [k, entry] : m_chunks) {
        if (entry.state == ChunkState::Ready && entry.modified) {
            Job job; job.type = JobType::Save;
            job.cx = k.cx; job.cy = k.cy; job.cz = k.cz;
            job.grid = std::make_unique<BlockGrid>();
            std::memcpy(job.grid->data, entry.chunk.blocks, sizeof(BlockGrid));
            enqueueJob(std::move(job));
        }
        releaseChunkResources(entry.chunk);
    }
    m_chunks.clear();
    { Job job; job.type = JobType::Flush; enqueueJob(std::move(job)); }
}

Physics* TerrainStreamer::getPhysics() const {
    if (!m_workspace) return nullptr;
    return m_workspace->getPhysicsEngine();
}

void TerrainStreamer::enqueueJob(Job&& job) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobQueue.push_back(std::move(job));
    }
    m_jobCv.notify_one();
}

void TerrainStreamer::releaseChunkResources(Chunk& chunk) {
    if (chunk.physicsActor) {
        Physics* phys = getPhysics();
        if (phys && phys->getScene()) phys->getScene()->removeActor(*chunk.physicsActor);
        chunk.physicsActor->release();
        chunk.physicsActor = nullptr;
    }
    if (chunk.mesh.VAO) { glDeleteVertexArrays(1, &chunk.mesh.VAO); chunk.mesh.VAO = 0; }
    if (chunk.mesh.VBO) { glDeleteBuffers(1, &chunk.mesh.VBO);      chunk.mesh.VBO = 0; }
    if (chunk.mesh.EBO) { glDeleteBuffers(1, &chunk.mesh.EBO);      chunk.mesh.EBO = 0; }
    chunk.mesh.indexCount = 0;
}

// ================================================================== //
//  update（メインスレッド）
// ================================================================== //
// 1フレームあたりの上限（バースト hitch 防止）
static constexpr int kMaxResultsPerFrame = 6;  // ロード結果の取り込み数
static constexpr int kMaxBuildsPerFrame  = 4;  // mesh/physics 構築数

void TerrainStreamer::update(const Vector3& playerPos)
{
    const int32_t pcx = worldToChunk(playerPos.x);
    const int32_t pcz = worldToChunk(playerPos.z);

    auto inRange = [&](int32_t cx, int32_t cy, int32_t cz) {
        return std::abs(cx - pcx) <= STREAM_RADIUS &&
               std::abs(cz - pcz) <= STREAM_RADIUS &&
               cy >= 0 && cy < 8;
    };

    // --- 範囲内の未登録チャンクをロード要求（プレースホルダを Loading で挿入）---
    for (int dx = -STREAM_RADIUS; dx <= STREAM_RADIUS; dx++)
    for (int cy = 0; cy < 8; cy++) // Y軸は 0～7 を常にロード
    for (int dz = -STREAM_RADIUS; dz <= STREAM_RADIUS; dz++) {
        ChunkKey key{ pcx+dx, cy, pcz+dz };
        if (m_chunks.find(key) != m_chunks.end()) continue;
        ChunkEntry& entry = m_chunks[key];
        entry.chunk.cx = key.cx; entry.chunk.cy = key.cy; entry.chunk.cz = key.cz;
        entry.state    = ChunkState::Loading;
        entry.modified = false;
        Job job; job.type = JobType::Load;
        job.cx = key.cx; job.cy = key.cy; job.cz = key.cz;
        enqueueJob(std::move(job));
    }

    // --- ワーカーからのロード結果を取り込む（上限あり）---
    for (int n = 0; n < kMaxResultsPerFrame; ++n) {
        LoadResult res;
        {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            if (m_resultQueue.empty()) break;
            res = std::move(m_resultQueue.front());
            m_resultQueue.pop_front();
        }
        auto it = m_chunks.find({res.cx, res.cy, res.cz});
        if (it == m_chunks.end() || it->second.state != ChunkState::Loading) continue; // 既に不要
        Chunk& chunk = it->second.chunk;
        std::memcpy(chunk.blocks, res.grid->data, sizeof(BlockGrid));
        it->second.state = ChunkState::Ready;
        chunk.mesh.dirty = true;
        // ノイズ生成チャンクはコーナー面取りをメインで分類（既存ファイル由来は形状確定済み）
        if (res.generated) {
            const int worldY0 = chunk.worldOriginY();
            for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                int wx = chunk.worldOriginX() + x;
                int wz = chunk.worldOriginZ() + z;
                int surfaceY = surfaceHeightFromNoise(wx, wz);
                if (surfaceY >= worldY0 && surfaceY < worldY0 + CHUNK_SIZE)
                    reclassifyColumnShape(wx, wz, /*persist=*/false); // 生成由来は保存しない
            }
        }
    }

    // --- 範囲外チャンクをアンロード ---
    std::vector<ChunkKey> toUnload;
    for (auto& [key, _] : m_chunks) {
        if (!inRange(key.cx, key.cy, key.cz)) toUnload.push_back(key);
    }
    for (auto& key : toUnload) {
        auto it = m_chunks.find(key);
        if (it == m_chunks.end()) continue;
        if (it->second.state == ChunkState::Ready) {
            if (it->second.modified) {
                Job job; job.type = JobType::Save;
                job.cx = key.cx; job.cy = key.cy; job.cz = key.cz;
                job.grid = std::make_unique<BlockGrid>();
                std::memcpy(job.grid->data, it->second.chunk.blocks, sizeof(BlockGrid));
                enqueueJob(std::move(job));
            }
            releaseChunkResources(it->second.chunk);
        }
        // Loading のものはプレースホルダを消すだけ（後続の結果はキー不在で破棄される）
        m_chunks.erase(it);
    }

    // --- dirty な Ready チャンクを再構築（上限あり）---
    int builds = 0;
    for (auto& [_, entry] : m_chunks) {
        if (entry.state != ChunkState::Ready || !entry.chunk.mesh.dirty) continue;
        rebuildIfDirty(entry);
        if (++builds >= kMaxBuildsPerFrame) break;
    }
}

void TerrainStreamer::rebuildIfDirty(ChunkEntry& entry)
{
    if (!entry.chunk.mesh.dirty) return;
    buildChunkMesh(entry.chunk, this);
    Physics* phys = getPhysics();
    if (phys) {
        buildChunkPhysics(entry.chunk, *phys);
        // レイキャストで地形だと識別できるよう、アクターに Terrain インスタンスを紐づける
        if (entry.chunk.physicsActor) entry.chunk.physicsActor->userData = m_owner;
    }
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

// 公開API: フラッシュをワーカーへ依頼する（同期書込はしない）
void TerrainStreamer::flushRegions() {
    Job job; job.type = JobType::Flush;
    enqueueJob(std::move(job));
}

// ワーカー専用: modified リージョンをディスクへ書き出す
void TerrainStreamer::flushRegionsToDisk() {
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

// ワーカー専用: 既存リージョンから該当チャンクを復元。データが無ければ false。
bool TerrainStreamer::decodeChunkFromRegion(int32_t cx, int32_t cy, int32_t cz, BlockGrid& grid)
{
    int32_t rx = chunkToRegion(cx);
    int32_t rz = chunkToRegion(cz);
    RegionCache& cache = getRegionCache(rx, rz);

    try {
        YAML::Node chunks = cache.root["chunks"];
        for (const auto& node : chunks) {
            if (node["cx"].as<int32_t>() != cx) continue;
            if (node["cy"].as<int32_t>() != cy) continue;
            if (node["cz"].as<int32_t>() != cz) continue;
            std::vector<RleEntry> rle;
            for (const auto& e : node["blocks"])
                rle.push_back({ e[0].as<int>(), e[1].as<uint8_t>(), e[2].as<uint8_t>(),
                                 e[3].as<uint8_t>(), e[4].as<uint8_t>(), e[5].as<uint8_t>() });
            rleDecode(rle, grid.data);
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[TerrainStreamer] YAML parse error: " << e.what() << std::endl;
    }
    return false;
}

// ワーカー専用: グリッドをリージョンキャッシュへエンコードして格納（modified マーク）
void TerrainStreamer::encodeChunkToRegion(int32_t cx, int32_t cy, int32_t cz, const BlockGrid& grid)
{
    int32_t rx = chunkToRegion(cx);
    int32_t rz = chunkToRegion(cz);
    RegionCache& cache = getRegionCache(rx, rz);

    YAML::Node& root = cache.root;

    // 既存エントリを除いた新シーケンスを構築
    YAML::Node newChunks(YAML::NodeType::Sequence);
    for (const auto& node : root["chunks"]) {
        bool match = node["cx"].as<int32_t>()==cx &&
                     node["cy"].as<int32_t>()==cy &&
                     node["cz"].as<int32_t>()==cz;
        if (!match) newChunks.push_back(node);
    }

    YAML::Node chunkNode;
    chunkNode["cx"] = cx;
    chunkNode["cy"] = cy;
    chunkNode["cz"] = cz;
    YAML::Node blocksNode(YAML::NodeType::Sequence);
    for (const auto& e : rleEncode(grid.data)) {
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
//  ワーカースレッド本体
// ================================================================== //
void TerrainStreamer::workerLoop()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCv.wait(lk, [&]{ return !m_jobQueue.empty(); });
            job = std::move(m_jobQueue.front());
            m_jobQueue.pop_front();
        }

        switch (job.type) {
        case JobType::Load: {
            auto grid = std::make_unique<BlockGrid>();
            bool found = decodeChunkFromRegion(job.cx, job.cy, job.cz, *grid);
            if (!found) generateRawGrid(job.cx, job.cy, job.cz, *grid);
            LoadResult res{ job.cx, job.cy, job.cz, std::move(grid), !found };
            {
                std::lock_guard<std::mutex> lk(m_resultMutex);
                m_resultQueue.push_back(std::move(res));
            }
            break;
        }
        case JobType::Save:
            if (job.grid) encodeChunkToRegion(job.cx, job.cy, job.cz, *job.grid);
            break;
        case JobType::Flush:
            flushRegionsToDisk();
            break;
        case JobType::SetDir:
            flushRegionsToDisk();
            m_regions.clear();
            terrainDir = job.dir;
            ensureDir(terrainDir);
            break;
        case JobType::Regenerate: {
            // 保存済み地形を破棄して新シード/モードへ。フラッシュはしない（破棄が目的）。
            m_noise.reseed(job.seed);
            m_flat = job.flat;
            m_regions.clear();
            std::error_code ec;
            std::filesystem::remove_all(terrainDir, ec); // DataPath を丸ごと削除
            ensureDir(terrainDir);
            break;
        }
        case JobType::Stop:
            flushRegionsToDisk();
            return;
        }
    }
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

int32_t TerrainStreamer::surfaceHeightFromNoise(int32_t wx, int32_t wz) const
{
    if (m_flat) return static_cast<int32_t>(TERRAIN_HEIGHT_MID); // 平坦地形
    float n = m_noise.fbm2(
        static_cast<float>(wx) * TERRAIN_SCALE,
        static_cast<float>(wz) * TERRAIN_SCALE,
        TERRAIN_OCTAVES,
        TERRAIN_PERSIST,
        TERRAIN_LACUNARITY);
    // n は [-1, 1] → ワールドY高さに変換
    return static_cast<int32_t>(TERRAIN_HEIGHT_MID + n * TERRAIN_HEIGHT_MAX);
}

// ワーカー専用: ノイズからブロックを生フィルする（reclassify はしない＝m_chunks に触れない）。
// コーナー面取りはメインスレッドが結果取り込み後に reclassifyColumnShape で行う。
void TerrainStreamer::generateRawGrid(int32_t cx, int32_t cy, int32_t cz, BlockGrid& grid)
{
    const int worldX0 = cx * CHUNK_SIZE;
    const int worldY0 = cy * CHUNK_SIZE;
    const int worldZ0 = cz * CHUNK_SIZE;

    for (int x = 0; x < CHUNK_SIZE; x++)
    for (int z = 0; z < CHUNK_SIZE; z++)
    {
        int wx = worldX0 + x;
        int wz = worldZ0 + z;
        int surfaceY = surfaceHeightFromNoise(wx, wz);

        for (int y = 0; y < CHUNK_SIZE; y++)
        {
            int wy = worldY0 + y;
            Block& b = grid.data[x][y][z];

            if (wy > surfaceY) {
                b.shape = BlockShape::Empty;
            } else if (wy == surfaceY) {
                b.shape    = BlockShape::Cube;
                b.material = BlockMaterial::Grass;
                b.r = 60; b.g = 140; b.b = 40;
            } else if (wy >= surfaceY - DIRT_DEPTH) {
                b.shape    = BlockShape::Cube;
                b.material = BlockMaterial::Dirt;
                b.r = 139; b.g = 90; b.b = 43;
            } else {
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
// Loading 状態（データ未到着）のチャンクは「未ロード扱い」で返す。
Chunk* TerrainStreamer::getChunk(int32_t cx, int32_t cy, int32_t cz) {
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end() || it->second.state != ChunkState::Ready) return nullptr;
    return &it->second.chunk;
}

const Block* TerrainStreamer::getBlockGlobal(int32_t wx, int32_t wy, int32_t wz) const {
    int32_t cx = (wx >= 0) ? (wx / CHUNK_SIZE) : ((wx - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cy = (wy >= 0) ? (wy / CHUNK_SIZE) : ((wy - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cz = (wz >= 0) ? (wz / CHUNK_SIZE) : ((wz - CHUNK_SIZE + 1) / CHUNK_SIZE);
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end() || it->second.state != ChunkState::Ready) return nullptr;

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

// ================================================================== //
//  コーナースムージング / ブラシ
// ================================================================== //

// ロード済みワールドYの上限（cy は 0..7 が常にロードされる前提。TerrainStreamer::update参照）
static constexpr int32_t LOADED_WORLD_Y_TOP = CHUNK_SIZE * 8 - 1;

int32_t TerrainStreamer::findSurfaceY(int32_t wx, int32_t wz) const {
    // チャンク単位でポインタを取得してから内部を走査する（getBlockGlobalをY1段ごとに
    // 呼ぶとハッシュ検索が大量に発生するため、チャンクの取得は cy ごとに1回だけにする）
    int32_t cx = (wx >= 0) ? (wx / CHUNK_SIZE) : ((wx - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cz = (wz >= 0) ? (wz / CHUNK_SIZE) : ((wz - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t bx = wx - cx * CHUNK_SIZE;
    int32_t bz = wz - cz * CHUNK_SIZE;

    for (int32_t cy = LOADED_WORLD_Y_TOP / CHUNK_SIZE; cy >= 0; cy--) {
        Chunk* chunk = const_cast<TerrainStreamer*>(this)->getChunk(cx, cy, cz);
        if (!chunk) return surfaceHeightFromNoise(wx, wz); // 未ロード範囲はノイズにフォールバック
        for (int32_t by = CHUNK_SIZE - 1; by >= 0; by--) {
            if (!chunk->blocks[bx][by][bz].isEmpty()) return cy * CHUNK_SIZE + by;
        }
    }
    return -1; // この列に地表が無い
}

void TerrainStreamer::reclassifyColumnShape(int32_t wx, int32_t wz, bool persist) {
    int32_t y = findSurfaceY(wx, wz);
    if (y < 0) return;

    int32_t cx = (wx >= 0) ? (wx / CHUNK_SIZE) : ((wx - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cy = (y  >= 0) ? (y  / CHUNK_SIZE) : ((y  - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int32_t cz = (wz >= 0) ? (wz / CHUNK_SIZE) : ((wz - CHUNK_SIZE + 1) / CHUNK_SIZE);
    Chunk* chunk = getChunk(cx, cy, cz);
    if (!chunk) return;

    Block& b = chunk->blocks[wx - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][wz - cz * CHUNK_SIZE];
    if (b.isEmpty()) return;

    const int32_t hN  = findSurfaceY(wx,     wz - 1);
    const int32_t hS  = findSurfaceY(wx,     wz + 1);
    const int32_t hE  = findSurfaceY(wx + 1, wz);
    const int32_t hW  = findSurfaceY(wx - 1, wz);
    const int32_t hNE = findSurfaceY(wx + 1, wz - 1);
    const int32_t hNW = findSurfaceY(wx - 1, wz - 1);
    const int32_t hSE = findSurfaceY(wx + 1, wz + 1);
    const int32_t hSW = findSurfaceY(wx - 1, wz + 1);

    // 直交隣接列が「ちょうど1段低い」か
    const bool nLow = (hN == y - 1);
    const bool sLow = (hS == y - 1);
    const bool eLow = (hE == y - 1);
    const bool wLow = (hW == y - 1);
    const int  orthoLow = (nLow?1:0) + (sLow?1:0) + (eLow?1:0) + (wLow?1:0);

    // 凸コーナーの面取り: あるコーナーで接する2辺の隣接列（直交方向）が両方とも
    // ちょうど1段低い場合、そのコーナーの頂点を削って斜面にする。
    // （斜め対角の高さは「角がきれいな段差か」の確認にだけ使う）
    const bool cutNE = (nLow && eLow && hNE <= y - 1);
    const bool cutNW = (nLow && wLow && hNW <= y - 1);
    const bool cutSE = (sLow && eLow && hSE <= y - 1);
    const bool cutSW = (sLow && wLow && hSW <= y - 1);
    const int cutCount = (cutNE?1:0) + (cutNW?1:0) + (cutSE?1:0) + (cutSW?1:0);

    // 注意: TRI_WEDGE_Top* のメッシュ定義は NE/SE のジオメトリが名前と入れ替わっている
    // （NW/SWは名前通り）。既存のメッシュテーブルは変更せず、ここで対応を補正する。
    BlockShape newShape = BlockShape::Cube;
    if (orthoLow == 1) {
        // まっすぐな1段の崖 → 三角柱ランプ（低い側へ斜面が下る）
        if (nLow)      newShape = BlockShape::Ramp_N;
        else if (sLow) newShape = BlockShape::Ramp_S;
        else if (eLow) newShape = BlockShape::Ramp_E;
        else           newShape = BlockShape::Ramp_W;
    } else if (cutCount == 1) {
        if (cutNE) newShape = BlockShape::Wedge_TopSE;
        else if (cutSE) newShape = BlockShape::Wedge_TopNE;
        else if (cutNW) newShape = BlockShape::Wedge_TopNW;
        else newShape = BlockShape::Wedge_TopSW;
    }

    if (b.shape != newShape) {
        if (persist) {
            // 編集由来 → 保存対象。writeBlock経由でDiffキャプチャにも反映する。
            Block copy = b;
            copy.shape = newShape;
            writeBlock(wx, y, wz, copy);
        } else {
            // 生成由来 → 再描画のみ（ノイズから再導出可能なので保存しない、modifiedは立てない）
            b.shape = newShape;
            if (Chunk* c = getChunk(cx, cy, cz)) {
                c->mesh.dirty = true;
            }
        }
    }
}

// 編集したチャンクを再構築対象かつ永続化対象としてマークする。
void TerrainStreamer::markChunkEdited(int32_t cx, int32_t cy, int32_t cz) {
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end()) return;
    it->second.chunk.mesh.dirty = true;
    it->second.modified         = true;
}

// ==== Diffキャプチャ / ブロック書き込みの一元化 ====

void TerrainStreamer::beginDiffCapture(std::vector<VoxelDiffEntry>* sink) {
    m_diffSink = sink;
    m_diffIndex.clear();
}

void TerrainStreamer::endDiffCapture() {
    m_diffSink = nullptr;
    m_diffIndex.clear();
}

// 1ブロックを書き換える下位処理。setBlock/raiseColumn/lowerColumn/reclassifyColumnShape の
// 共通実装。チャンク境界に接する隣接チャンクの mesh.dirty 化もここで行う（旧 setBlock の
// markNeighborMesh ロジックを踏襲）。
void TerrainStreamer::writeBlock(int32_t wx, int32_t wy, int32_t wz, const Block& value) {
    const int32_t cx = blockToChunk(wx);
    const int32_t cy = blockToChunk(wy);
    const int32_t cz = blockToChunk(wz);
    Chunk* chunk = getChunk(cx, cy, cz);
    if (!chunk) return; // 未ロードのチャンクは編集しない

    Block& blk = chunk->blocks[wx - cx*CHUNK_SIZE][wy - cy*CHUNK_SIZE][wz - cz*CHUNK_SIZE];
    Block before = blk;
    if (before.shape == value.shape && before.material == value.material &&
        before.r == value.r && before.g == value.g && before.b == value.b) {
        return; // 値が同じなら書き込み不要
    }

    if (m_diffSink) {
        uint64_t key = packVoxelKey(wx, wy, wz);
        auto it = m_diffIndex.find(key);
        if (it != m_diffIndex.end()) {
            (*m_diffSink)[it->second].after = value;
        } else {
            m_diffIndex.emplace(key, m_diffSink->size());
            m_diffSink->push_back(VoxelDiffEntry{wx, wy, wz, before, value});
        }
    }

    blk = value;
    markChunkEdited(cx, cy, cz);

    // チャンク境界に接する場合は、隣接チャンクの面カリング再構築のため dirty にする
    // （隣接チャンクのデータ自体は変わらないので modified は立てない）。
    auto markNeighborMesh = [&](int32_t ncx, int32_t ncy, int32_t ncz) {
        if (ncx == cx && ncy == cy && ncz == cz) return;
        if (Chunk* nb = getChunk(ncx, ncy, ncz)) nb->mesh.dirty = true;
    };
    markNeighborMesh(blockToChunk(wx-1), cy, cz);
    markNeighborMesh(blockToChunk(wx+1), cy, cz);
    markNeighborMesh(cx, blockToChunk(wy-1), cz);
    markNeighborMesh(cx, blockToChunk(wy+1), cz);
    markNeighborMesh(cx, cy, blockToChunk(wz-1));
    markNeighborMesh(cx, cy, blockToChunk(wz+1));
}

void TerrainStreamer::raiseColumn(int32_t wx, int32_t wz) {
    int32_t y = findSurfaceY(wx, wz);
    if (y < 0 || y + 1 > LOADED_WORLD_Y_TOP) return;
    const Block* src = getBlockGlobal(wx, y, wz);
    if (!src) return;
    Block copy = *src;
    copy.shape = BlockShape::Cube;

    int32_t newY = y + 1;
    writeBlock(wx, newY, wz, copy);
}

void TerrainStreamer::lowerColumn(int32_t wx, int32_t wz) {
    int32_t y = findSurfaceY(wx, wz);
    if (y < 0) return;

    const Block* src = getBlockGlobal(wx, y, wz);
    if (!src) return;
    Block copy = *src;
    copy.shape = BlockShape::Empty;
    writeBlock(wx, y, wz, copy);
}

void TerrainStreamer::applyBrush(const Vector3& worldPos, float radius, int mode) {
    // ブロック中心は (ブロック番号 * BLOCK_STUD_SIZE) にあるため、最近傍へ丸める
    // （floor だとブロックの下半分で1ブロックずれる）
    const int32_t centerWx = static_cast<int32_t>(std::lround(worldPos.x / BLOCK_STUD_SIZE));
    const int32_t centerWz = static_cast<int32_t>(std::lround(worldPos.z / BLOCK_STUD_SIZE));
    const int32_t blockRadius = (std::max)(1, static_cast<int32_t>(std::ceil(radius / BLOCK_STUD_SIZE)));
    const float   radiusSq = radius * radius;

    std::vector<std::pair<int32_t,int32_t>> touchedColumns;
    touchedColumns.reserve((size_t)(blockRadius*2+1) * (size_t)(blockRadius*2+1));

    for (int32_t dx = -blockRadius; dx <= blockRadius; dx++)
    for (int32_t dz = -blockRadius; dz <= blockRadius; dz++) {
        float wsx = (float)(centerWx + dx) * BLOCK_STUD_SIZE - worldPos.x;
        float wsz = (float)(centerWz + dz) * BLOCK_STUD_SIZE - worldPos.z;
        if (wsx*wsx + wsz*wsz > radiusSq) continue;

        int32_t wx = centerWx + dx;
        int32_t wz = centerWz + dz;

        if (mode > 0) {
            raiseColumn(wx, wz);
        } else if (mode < 0) {
            lowerColumn(wx, wz);
        } else {
            // Smooth: 周囲8列の平均高さに1段だけ近づける
            int32_t y = findSurfaceY(wx, wz);
            if (y < 0) continue;
            int32_t sum = 0, count = 0;
            for (int32_t ndx = -1; ndx <= 1; ndx++)
            for (int32_t ndz = -1; ndz <= 1; ndz++) {
                if (ndx == 0 && ndz == 0) continue;
                int32_t ny = findSurfaceY(wx + ndx, wz + ndz);
                if (ny < 0) continue;
                sum += ny; count++;
            }
            if (count == 0) continue;
            int32_t avgY = (int32_t)std::lround((double)sum / (double)count);
            if (avgY > y) raiseColumn(wx, wz);
            else if (avgY < y) lowerColumn(wx, wz);
        }

        touchedColumns.emplace_back(wx, wz);
    }

    // 影響を受けた列とその斜め近傍列を再分類し、関係する全チャンクを再構築対象にする
    static constexpr int32_t kOffsets[9][2] = {
        {0,0}, {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}
    };
    for (auto& [wx, wz] : touchedColumns) {
        for (auto& off : kOffsets) {
            reclassifyColumnShape(wx + off[0], wz + off[1], /*persist=*/true); // 編集由来は保存
        }
    }

    // 地形を編集したのでナビメッシュキャッシュは無効化し、次回FindPathで再構築させる
    PathfindingService::InvalidateActive(m_workspace);
}

bool TerrainStreamer::findSurfaceAlongAxis(int32_t axis, int32_t sign, int32_t a, int32_t b,
                                           int32_t searchOrigin, int32_t searchRange, int32_t& outCoord) const {
    // searchOrigin（レイが実際に当たった座標）を起点に局所的に実体/空洞の境界を探す。
    // 探索ウィンドウ全体を端からスキャンすると、空洞内など複数の実体/空洞層がある場合に
    // 無関係な遠方の地形を表層として誤検出してしまうため、起点からの局所探索に限定する。
    auto isSolid = [&](int32_t c) -> bool {
        if (axis == 1 && (c < 0 || c > LOADED_WORLD_Y_TOP)) return false;
        int32_t wx, wy, wz;
        axisToWorld(axis, a, b, c, wx, wy, wz);
        const Block* blk = getBlockGlobal(wx, wy, wz);
        return blk && !blk->isEmpty();
    };

    if (isSolid(searchOrigin)) {
        // 起点が実体: sign方向へ実体が続く限り進む（局所的な出っ張りに対応）
        int32_t c = searchOrigin;
        int32_t steps = 0;
        while (steps < searchRange && isSolid(c + sign)) { c += sign; steps++; }
        outCoord = c;
        return true;
    } else {
        // 起点が空洞: -sign方向（実体があるはずの側）へ実体が見つかるまで進む（局所的な凹みに対応）
        int32_t c = searchOrigin;
        for (int32_t steps = 0; steps < searchRange; steps++) {
            c -= sign;
            if (isSolid(c)) { outCoord = c; return true; }
        }
        return false;
    }
}

void TerrainStreamer::applyDirectionalBrush(const Vector3& worldPos, int32_t axis, int32_t sign,
                                            float radius, int mode) {
    // 従来の「上から」ケースは既存 applyBrush をそのまま使う（Ramp/Wedge自動スロープを維持）。
    if (axis == 1 && sign > 0) {
        applyBrush(worldPos, radius, mode);
        return;
    }
    if (mode == 0) return; // Smoothは横方向未対応

    // axisでない残り2軸(tanA, tanB)を決める（axisToWorldの規約と揃える）
    int32_t tanA, tanB;
    if (axis == 0)      { tanA = 1; tanB = 2; }
    else if (axis == 1) { tanA = 0; tanB = 2; }
    else                { tanA = 0; tanB = 1; }

    static constexpr int32_t kSearchRange = 64;

    const float posA = axisComponent(worldPos, tanA);
    const float posB = axisComponent(worldPos, tanB);
    const int32_t centerA = static_cast<int32_t>(std::lround(posA / BLOCK_STUD_SIZE));
    const int32_t centerB = static_cast<int32_t>(std::lround(posB / BLOCK_STUD_SIZE));
    const int32_t searchOrigin = static_cast<int32_t>(std::lround(axisComponent(worldPos, axis) / BLOCK_STUD_SIZE));
    const int32_t blockRadius = (std::max)(1, static_cast<int32_t>(std::ceil(radius / BLOCK_STUD_SIZE)));
    const float   radiusSq = radius * radius;

    bool touchedAny = false;
    for (int32_t da = -blockRadius; da <= blockRadius; da++)
    for (int32_t db = -blockRadius; db <= blockRadius; db++) {
        float wsa = (float)(centerA + da) * BLOCK_STUD_SIZE - posA;
        float wsb = (float)(centerB + db) * BLOCK_STUD_SIZE - posB;
        if (wsa*wsa + wsb*wsb > radiusSq) continue;

        int32_t a = centerA + da;
        int32_t b = centerB + db;
        int32_t surfCoord;
        if (!findSurfaceAlongAxis(axis, sign, a, b, searchOrigin, kSearchRange, surfCoord)) continue;

        int32_t swx, swy, swz;
        axisToWorld(axis, a, b, surfCoord, swx, swy, swz);

        if (mode > 0) {
            const Block* src = getBlockGlobal(swx, swy, swz);
            if (!src) continue;
            Block copy = *src;
            copy.shape = BlockShape::Cube;

            int32_t targetCoord = surfCoord + sign;
            if (axis == 1 && targetCoord > LOADED_WORLD_Y_TOP) continue;

            int32_t twx, twy, twz;
            axisToWorld(axis, a, b, targetCoord, twx, twy, twz);
            writeBlock(twx, twy, twz, copy);
        } else {
            Block empty;
            empty.shape = BlockShape::Empty;
            empty.r = empty.g = empty.b = 0;
            writeBlock(swx, swy, swz, empty);
        }
        touchedAny = true;
    }

    if (touchedAny) {
        PathfindingService::InvalidateActive(m_workspace);
    }
}

void TerrainStreamer::applyColorBrush(const Vector3& worldPos, int32_t axis, int32_t sign,
                                      float radius, uint8_t r, uint8_t g, uint8_t b) {
    int32_t tanA, tanB;
    if (axis == 0)      { tanA = 1; tanB = 2; }
    else if (axis == 1) { tanA = 0; tanB = 2; }
    else                { tanA = 0; tanB = 1; }

    static constexpr int32_t kSearchRange = 64;

    const float posA = axisComponent(worldPos, tanA);
    const float posB = axisComponent(worldPos, tanB);
    const int32_t centerA = static_cast<int32_t>(std::lround(posA / BLOCK_STUD_SIZE));
    const int32_t centerB = static_cast<int32_t>(std::lround(posB / BLOCK_STUD_SIZE));
    const int32_t searchOrigin = static_cast<int32_t>(std::lround(axisComponent(worldPos, axis) / BLOCK_STUD_SIZE));
    const int32_t blockRadius = (std::max)(1, static_cast<int32_t>(std::ceil(radius / BLOCK_STUD_SIZE)));
    const float   radiusSq = radius * radius;

    for (int32_t da = -blockRadius; da <= blockRadius; da++)
    for (int32_t db = -blockRadius; db <= blockRadius; db++) {
        float wsa = (float)(centerA + da) * BLOCK_STUD_SIZE - posA;
        float wsb = (float)(centerB + db) * BLOCK_STUD_SIZE - posB;
        if (wsa*wsa + wsb*wsb > radiusSq) continue;

        int32_t a  = centerA + da;
        int32_t tb = centerB + db;
        int32_t surfCoord;
        if (!findSurfaceAlongAxis(axis, sign, a, tb, searchOrigin, kSearchRange, surfCoord)) continue;

        int32_t swx, swy, swz;
        axisToWorld(axis, a, tb, surfCoord, swx, swy, swz);

        const Block* src = getBlockGlobal(swx, swy, swz);
        if (!src) continue;
        Block copy = *src;
        copy.r = r; copy.g = g; copy.b = b;
        writeBlock(swx, swy, swz, copy);
    }
}

bool TerrainStreamer::raycastVoxel(const Vector3& origin, const Vector3& dir, float maxDist, Vector3& outHit) const {
    int32_t bx, by, bz, axis, sign;
    return raycastVoxelFace(origin, dir, maxDist, outHit, bx, by, bz, axis, sign);
}

bool TerrainStreamer::raycastVoxelFace(const Vector3& origin, const Vector3& dir, float maxDist,
                                       Vector3& outHit, int32_t& outBx, int32_t& outBy, int32_t& outBz,
                                       int32_t& outAxis, int32_t& outSign) const {
    const float bs = BLOCK_STUD_SIZE;

    // ブロック中心は index*bs にあり、ブロックは [(index-0.5)*bs, (index+0.5)*bs] を占める。
    int32_t bx = (int32_t)std::floor(origin.x / bs + 0.5f);
    int32_t by = (int32_t)std::floor(origin.y / bs + 0.5f);
    int32_t bz = (int32_t)std::floor(origin.z / bs + 0.5f);

    const int32_t stepX = (dir.x > 0.0f) ? 1 : ((dir.x < 0.0f) ? -1 : 0);
    const int32_t stepY = (dir.y > 0.0f) ? 1 : ((dir.y < 0.0f) ? -1 : 0);
    const int32_t stepZ = (dir.z > 0.0f) ? 1 : ((dir.z < 0.0f) ? -1 : 0);

    constexpr float INF = 1e30f;
    // 各軸: 次のブロック境界までの t と、1ブロック進むのに要する t
    auto setup = [&](int32_t b, float o, float d, int32_t step, float& tMax, float& tDelta) {
        if (step == 0) { tMax = INF; tDelta = INF; return; }
        float boundary = ((float)b + 0.5f * (float)step) * bs; // 進行方向側の境界
        tMax   = (boundary - o) / d;
        tDelta = bs / std::abs(d);
    };
    float tMaxX, tMaxY, tMaxZ, tDeltaX, tDeltaY, tDeltaZ;
    setup(bx, origin.x, dir.x, stepX, tMaxX, tDeltaX);
    setup(by, origin.y, dir.y, stepY, tMaxY, tDeltaY);
    setup(bz, origin.z, dir.z, stepZ, tMaxZ, tDeltaZ);

    float t = 0.0f; // 現在ブロックに入った時点の t（= ブロック表面の交点）
    // 直前に進んだ軸(0=X,1=Y,2=Z)とそのステップ方向。まだ一度も進んでいなければ outAxis=1,outSign=1 にフォールバックする。
    int32_t lastAxis = 1, lastStep = -1;
    for (int guard = 0; guard < 8192; ++guard) {
        const Block* blk = getBlockGlobal(bx, by, bz);
        if (blk && !blk->isEmpty()) {
            outHit = origin + dir * t;
            outBx = bx; outBy = by; outBz = bz;
            outAxis = lastAxis;
            outSign = -lastStep; // 面の外向き法線 = 直前のステップ方向と逆符号
            return true;
        }
        // 最も近い境界の軸へ1ブロック進む
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            bx += stepX; t = tMaxX; tMaxX += tDeltaX; lastAxis = 0; lastStep = stepX;
        } else if (tMaxY <= tMaxZ) {
            by += stepY; t = tMaxY; tMaxY += tDeltaY; lastAxis = 1; lastStep = stepY;
        } else {
            bz += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; lastAxis = 2; lastStep = stepZ;
        }
        if (t > maxDist) return false;
    }
    return false;
}

bool TerrainStreamer::setBlock(int32_t wx, int32_t wy, int32_t wz,
                               BlockShape shape, uint8_t r, uint8_t g, uint8_t b) {
    const int32_t cx = blockToChunk(wx);
    const int32_t cy = blockToChunk(wy);
    const int32_t cz = blockToChunk(wz);
    Chunk* chunk = getChunk(cx, cy, cz);
    if (!chunk) return false; // 未ロードのチャンクは編集しない

    const Block* cur = getBlockGlobal(wx, wy, wz);
    Block value;
    value.shape    = shape;
    value.material = cur ? cur->material : BlockMaterial::Stone;
    value.r = r; value.g = g; value.b = b;
    writeBlock(wx, wy, wz, value);
    return true;
}

bool TerrainStreamer::removeBlock(int32_t wx, int32_t wy, int32_t wz) {
    return setBlock(wx, wy, wz, BlockShape::Empty, 0, 0, 0);
}