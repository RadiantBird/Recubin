#pragma once
#include <include/Core/Terrain.hpp>
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
    static constexpr int   STREAM_RADIUS    = 4;
    static constexpr int   REGION_SIZE      = 32;
    static constexpr float BLOCK_STUD_SIZE  = 4.0f; // 1ブロックのワールドサイズ（studs）

    std::string terrainDir = "terrain";

    // owner: このストリーマーを所有する Terrain インスタンス。
    // チャンクの物理アクターの userData に設定し、レイキャストで地形だと識別できるようにする。
    TerrainStreamer(Workspace* workspace, Instance* owner = nullptr);

    ~TerrainStreamer();

    // Workspace が切り替わったとき（ed->hierarchyPanel->onSwitchWorkspace など）に呼ぶ
    void setWorkspace(Workspace* workspace);

    // 全チャンクを解放する
    void clear();

    // 毎フレーム呼ぶ。playerPos はワールド座標（ブロック単位）。
    void update(const Vector3& playerPos);

    // チャンクを直接取得（デバッグ・ブラシ用）
    Chunk* getChunk(int32_t cx, int32_t cy, int32_t cz);

    // ワールド座標からブロックを取得（カリング等で使用）
    const Block* getBlockGlobal(int32_t wx, int32_t wy, int32_t wz) const;

    // チャンクを dirty にして次フレームで再メッシュ・再物理させる
    void markDirty(int32_t cx, int32_t cy, int32_t cz);

    // キャッシュされたリージョンデータをファイルに書き出す
    void flushRegions();

    // 指定列(wx,wz)の表層Y座標を求める。チャンクが未ロードならノイズから推定する。
    int32_t findSurfaceY(int32_t wx, int32_t wz) const;

    // 指定列の表層ブロックの形状を、斜め近傍列の高さに応じて Cube / Wedge_Top* に分類し直す。
    // 呼び出し元が対象チャンクを markDirty する必要がある。
    void reclassifyColumnShape(int32_t wx, int32_t wz);

    // ブラシ編集。mode: +1=Raise（1段積む）, -1=Lower（1段削る）, 0=Smooth（周囲8列の平均に1段近づける）。
    // worldPos を中心に半径 radius（studs）内のXZ列を処理し、影響範囲を再分類・再構築する。
    void applyBrush(const Vector3& worldPos, float radius, int mode);

    // ブロックデータに対して直接レイキャストする（ボクセルDDA）。物理シーンを介さないため
    // 編集直後でも常に最新の地形を参照でき、PhysXのSQ未更新による貫通が起きない。
    // ヒット時は outHit にブロック表面の交点（ワールド座標）を入れて true を返す。
    bool raycastVoxel(const Vector3& origin, const Vector3& dir, float maxDist, Vector3& outHit) const;

private:
    Workspace* m_workspace; // 所有しない、ライフタイムは呼び出し元が管理
    Instance*  m_owner;     // 所有しない。物理アクターの userData に設定する Terrain インスタンス
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

public:
    // ロード済みチャンク一覧（Renderer描画用）
    const std::unordered_map<ChunkKey, ChunkEntry, ChunkKeyHash>& getChunks() const { return m_chunks; }

private:
    struct RegionKey {
        int32_t rx, rz;
        bool operator==(const RegionKey& o) const { return rx==o.rx && rz==o.rz; }
    };
    struct RegionKeyHash {
        size_t operator()(const RegionKey& k) const {
            size_t h = 2166136261u;
            auto mix = [&](int32_t v) { h ^= (size_t)v; h *= 16777619u; };
            mix(k.rx); mix(k.rz);
            return h;
        }
    };
    struct RegionCache {
        YAML::Node root;
        bool loaded = false;
        bool modified = false;
    };
    std::unordered_map<RegionKey, RegionCache, RegionKeyHash> m_regions;

    void loadChunk  (int32_t cx, int32_t cy, int32_t cz);
    void unloadChunk(int32_t cx, int32_t cy, int32_t cz);
    void rebuildIfDirty(ChunkEntry& entry);

    static std::string regionPath(const std::string& dir, int32_t rx, int32_t rz);
    RegionCache& getRegionCache(int32_t rx, int32_t rz);
    void readChunkFromRegion (Chunk& chunk);
    void writeChunkToRegion  (const Chunk& chunk);
    void generateChunk       (Chunk& chunk);

    // ノイズから列(wx,wz)の地表Y座標を直接計算する（ブロック未生成時のフォールバック用）
    int32_t surfaceHeightFromNoise(int32_t wx, int32_t wz) const;

    // 列(wx,wz)の表層を1段積む／削る（ブラシ・スムージング共通の下位処理）
    void raiseColumn(int32_t wx, int32_t wz);
    void lowerColumn(int32_t wx, int32_t wz);

    // Physics を安全に取得（nullptr の場合は物理生成をスキップ）
    Physics* getPhysics() const;
};