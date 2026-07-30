#pragma once
#include <include/Core/Terrain.hpp>
#include <include/Instances/Workspace.hpp>
#include <Math/PerlinNoise.hpp>
#include <include/Math/Vector3.hpp>
#include <unordered_map>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <memory>

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

    // 地形データの保存先。コンストラクタ or setDataPath() 経由でのみ変更すること
    // （ワーカースレッドが読むため、外から直接書き換えないこと）。
    std::string terrainDir = "terrain";

    // owner: このストリーマーを所有する Terrain インスタンス。
    // チャンクの物理オブジェクトの userData に設定し、レイキャストで地形だと識別できるようにする。
    TerrainStreamer(Workspace* workspace, Instance* owner = nullptr, const std::string& dataDir = "terrain",
                    uint32_t seed = 12345u, bool flat = false);

    ~TerrainStreamer();

    // Workspace が切り替わったとき（ed->hierarchyPanel->onSwitchWorkspace など）に呼ぶ
    void setWorkspace(Workspace* workspace);

    // データ保存先ディレクトリを切り替える（旧データを保存してから新ディレクトリを再ロード）。
    void setDataPath(const std::string& dir);

    // 地形を作り直す。DataPath の保存済み地形（編集含む）を破棄し、新シード/モードで再生成する。
    void regenerate(uint32_t seed, bool flat);

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

    // 指定列の表層ブロックの形状を、近傍列の高さに応じて Cube / Wedge / Ramp に分類し直す。
    // persist=true（ブラシ/API編集）なら永続化対象(modified)にする。
    // persist=false（生成時）は形状がノイズから再導出可能なので dirty のみ（保存しない）。
    void reclassifyColumnShape(int32_t wx, int32_t wz, bool persist);

    // ブラシ編集。mode: +1=Raise（1段積む）, -1=Lower（1段削る）, 0=Smooth（周囲8列の平均に1段近づける）。
    // worldPos を中心に半径 radius（studs）内のXZ列を処理し、影響範囲を再分類・再構築する。
    void applyBrush(const Vector3& worldPos, float radius, int mode);

    // ブロックデータに対して直接レイキャストする（ボクセルDDA）。物理シーンを介さないため
    // 編集直後でも常に最新の地形を参照でき、PhysXのSQ未更新による貫通が起きない。
    // ヒット時は outHit にブロック表面の交点（ワールド座標）を入れて true を返す。
    bool raycastVoxel(const Vector3& origin, const Vector3& dir, float maxDist, Vector3& outHit) const;

    // ワールド「ブロック座標」で1ブロックを書き換える／削除する（部分編集API）。
    // 対象チャンクが未ロードなら false。編集チャンクは永続化対象としてマークされる。
    bool setBlock(int32_t wx, int32_t wy, int32_t wz, BlockShape shape, uint8_t r, uint8_t g, uint8_t b);
    bool removeBlock(int32_t wx, int32_t wy, int32_t wz);

    // ブラシ編集1回分の差分（Undo/Redo用）。CommandHistory から参照される。
    struct VoxelDiffEntry {
        int32_t wx, wy, wz;
        Block   before, after;
    };

    // 以降の writeBlock 呼び出しの差分を sink に記録する。end まで有効。
    void beginDiffCapture(std::vector<VoxelDiffEntry>* sink);
    void endDiffCapture();

    // 面法線つきボクセルレイキャスト。ヒットしたブロック座標(outBx/By/Bz)と、
    // ヒットした面の軸(outAxis: 0=X,1=Y,2=Z)・外向き符号(outSign: ±1)を返す。
    bool raycastVoxelFace(const Vector3& origin, const Vector3& dir, float maxDist,
                          Vector3& outHit, int32_t& outBx, int32_t& outBy, int32_t& outBz,
                          int32_t& outAxis, int32_t& outSign) const;

    // 6方向対応Sculptブラシ。axis(0=X,1=Y,2=Z)/sign(±1)で示す方向へRaise/Lowerする。
    // axis==1,sign==+1（上から）は既存 applyBrush と完全に同じ挙動（Ramp/Wedge自動スロープ含む）。
    void applyDirectionalBrush(const Vector3& worldPos, int32_t axis, int32_t sign,
                                float radius, int mode);

    // Paintブラシ。指定方向から見える表層ブロックの色だけを書き換える（形状は不変）。
    void applyColorBrush(const Vector3& worldPos, int32_t axis, int32_t sign,
                          float radius, uint8_t r, uint8_t g, uint8_t b);

private:
    Workspace* m_workspace; // 所有しない、ライフタイムは呼び出し元が管理
    Instance*  m_owner;     // 所有しない。物理オブジェクトの userData に設定する Terrain インスタンス
    PerlinNoise m_noise;
    bool        m_flat = false; // true なら平坦地形を生成

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

    // 非同期ロード中(Loading)はメッシュ未生成・データ未到着のため描画/参照対象外とする。
    enum class ChunkState { Loading, Ready };

    struct ChunkEntry {
        Chunk      chunk;
        bool       modified = false;
        ChunkState state    = ChunkState::Loading;
    };

    std::unordered_map<ChunkKey, ChunkEntry, ChunkKeyHash> m_chunks;

    // ワーカー↔メイン間で受け渡すブロックデータ（Chunk.blocks と同レイアウト）。
    struct BlockGrid {
        Block data[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    };

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
    // ==== リージョンキャッシュ・ディスクI/O（ワーカースレッド専用。メインから触らない）====
    std::unordered_map<RegionKey, RegionCache, RegionKeyHash> m_regions;

    static std::string regionPath(const std::string& dir, int32_t rx, int32_t rz);
    RegionCache& getRegionCache(int32_t rx, int32_t rz);                 // worker
    bool decodeChunkFromRegion(int32_t cx, int32_t cy, int32_t cz, BlockGrid& grid); // worker: 既存データを復元、無ければfalse
    void encodeChunkToRegion  (int32_t cx, int32_t cy, int32_t cz, const BlockGrid& grid); // worker
    void flushRegionsToDisk();                                          // worker
    void generateRawGrid(int32_t cx, int32_t cy, int32_t cz, BlockGrid& grid); // worker: ノイズの生フィルのみ

    // ==== ワーカースレッド基盤 ====
    enum class JobType { Load, Save, Flush, SetDir, Regenerate, Stop };
    struct Job {
        JobType type;
        int32_t cx = 0, cy = 0, cz = 0;
        std::unique_ptr<BlockGrid> grid; // Save 用
        std::string dir;                 // SetDir 用
        uint32_t seed = 0;               // Regenerate 用
        bool     flat = false;           // Regenerate 用
    };
    struct LoadResult {
        int32_t cx, cy, cz;
        std::unique_ptr<BlockGrid> grid;
        bool generated; // ノイズ生成なら true（メインで reclassify が必要）
    };

    std::thread             m_worker;
    std::mutex              m_jobMutex;
    std::condition_variable m_jobCv;
    std::deque<Job>         m_jobQueue;
    std::mutex             m_resultMutex;
    std::deque<LoadResult> m_resultQueue;

    void workerLoop();
    void enqueueJob(Job&& job);

    // ==== メインスレッド側 ====
    void releaseChunkResources(Chunk& chunk); // GL/物理リソースを解放（メイン）
    void rebuildIfDirty(ChunkEntry& entry);   // mesh/physics をビルド（メイン）

    // ノイズから列(wx,wz)の地表Y座標を直接計算する（ブロック未生成時のフォールバック用。const読取りでスレッド安全）
    int32_t surfaceHeightFromNoise(int32_t wx, int32_t wz) const;

    // 列(wx,wz)の表層を1段積む／削る（ブラシ・スムージング共通の下位処理）
    void raiseColumn(int32_t wx, int32_t wz);
    void lowerColumn(int32_t wx, int32_t wz);

    // 編集したチャンクを再構築対象(mesh.dirty)かつ永続化対象(entry.modified)としてマークする。
    void markChunkEdited(int32_t cx, int32_t cy, int32_t cz);

    // Physics を安全に取得（nullptr の場合は物理生成をスキップ）
    Physics* getPhysics() const;

    // ==== Diffキャプチャ / ブロック書き込みの一元化 ====
    // beginDiffCapture() 〜 endDiffCapture() の間に writeBlock() で書き換えられた
    // ブロックは m_diffSink に (wx,wy,wz,before,after) として記録される。
    std::vector<VoxelDiffEntry>*         m_diffSink = nullptr;
    std::unordered_map<uint64_t, size_t> m_diffIndex; // (wx,wy,wz)パック→m_diffSink内index

    // 1ブロックを書き換える下位処理。setBlock/raiseColumn/lowerColumn/reclassifyColumnShape の
    // 共通実装。チャンク境界に接する隣接チャンクの mesh.dirty 化もここで行う。
    void writeBlock(int32_t wx, int32_t wy, int32_t wz, const Block& value);

    // axis(0=X,1=Y,2=Z)/sign(±1)方向へ、(a,b)を残り2軸の座標として表層ブロックのaxis座標を探す。
    // findSurfaceY の汎用版（フォールバック無し、未ロードチャンクはスキップ）。
    bool findSurfaceAlongAxis(int32_t axis, int32_t sign, int32_t a, int32_t b,
                               int32_t searchOrigin, int32_t searchRange, int32_t& outCoord) const;
};
