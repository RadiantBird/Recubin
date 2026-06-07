#pragma once
#include <include/Core/Terrain.hpp>
#include <include/Core/Renderer.hpp>
#include <include/Instances/Workspace.hpp>
#include <Math/PerlinNoise.hpp>
#include <include/Math/Vector3.hpp>
#include <unordered_map>
#include <string>

// ================================================================== //
//  TerrainStreamer
//
//  毎フレーム update(playerWorldPos) を呼ぶだけで
//  ・半径 STREAM_RADIUS チャンク以内 → ロード
//  ・範囲外 → アンロード（変更があればファイルに書き戻し）
//  を同期で処理する。
//
//  Physics は Workspace::getPhysicsEngine() 経由で取得する。
//  （main.cpp の設計に合わせ、Physics への直接参照は持たない）
//
//  ファイル構造:
//    terrain/r_{rx}_{rz}.yaml  （1リージョン = 32×32 チャンク × 全y層）
// ================================================================== //

class TerrainStreamer {
public:
    static constexpr int STREAM_RADIUS = 4;
    static constexpr int REGION_SIZE   = 32;

    std::string terrainDir = "terrain";

    TerrainStreamer(Renderer* renderer, Workspace* workspace);

    ~TerrainStreamer();

    // Workspace が切り替わったとき（ed->hierarchyPanel->onSwitchWorkspace など）に呼ぶ
    void setWorkspace(Workspace* workspace);

    // 全チャンクを解放する
    void clear();

    // 毎フレーム呼ぶ。playerPos はワールド座標（ブロック単位）。
    void update(const Vector3& playerPos);

    // チャンクを直接取得（デバッグ・ブラシ用）
    Chunk* getChunk(int32_t cx, int32_t cy, int32_t cz);

    // チャンクを dirty にして次フレームで再メッシュ・再物理させる
    void markDirty(int32_t cx, int32_t cy, int32_t cz);

private:
    Renderer*  m_renderer;
    Workspace* m_workspace; // 所有しない、ライフタイムは呼び出し元が管理
    PerlinNoise m_noise;

    struct ChunkKey {
        int32_t cx, cy, cz;
        bool operator==(const ChunkKey& o) const {
            return cx==o.cx && cy==o.cy && cz==o.cz;
        }
    };
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& k) const {
            size_t h = 2166136261u;
            auto mix = [&](int32_t v) { h ^= (size_t)v; h *= 16777619u; };
            mix(k.cx); mix(k.cy); mix(k.cz);
            return h;
        }
    };

    struct ChunkEntry {
        Chunk chunk;
        bool  modified = false;
    };

    std::unordered_map<ChunkKey, ChunkEntry, ChunkKeyHash> m_chunks;

    void loadChunk  (int32_t cx, int32_t cy, int32_t cz);
    void unloadChunk(int32_t cx, int32_t cy, int32_t cz);
    void rebuildIfDirty(ChunkEntry& entry);

    static std::string regionPath(const std::string& dir, int32_t rx, int32_t rz);
    void readChunkFromRegion (Chunk& chunk);
    void writeChunkToRegion  (const Chunk& chunk);
    void generateChunk       (Chunk& chunk);

    // Physics を安全に取得（nullptr の場合は物理生成をスキップ）
    Physics* getPhysics() const;
};