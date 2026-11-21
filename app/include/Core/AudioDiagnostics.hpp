#pragma once
#include "miniaudio.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Sound;

// ===================================================
//  AudioDiagnostics — 音声の異常調査用の計測。
//   - 出力タップ: グループとエンジン出力の間に置いたパススルーノードで、ブロックごとに
//     ピーク・RMS・サンプル間の最大差（クリック指標）・NaN・クリップ・コールバック間隔を測る。
//   - Soundの状態: 毎フレーム、再生位置・音量・距離・パン・再生中かをサンプリングする。
//   - Soundの呼び出しイベント: Play/Stop/Reset/Seekの前後の状態を記録する。
//  オーディオスレッドが書くのはタップのリングバッファだけ。それ以外はメインスレッド専用。
// ===================================================
namespace AudioDiag {

struct BlockStat {
    double time = 0.0;               // コールバックの時刻（秒、steady_clock基準）
    std::uint32_t frames = 0;
    float peak[2] = {0.0f, 0.0f};    // 0=左, 1=右（モノラルは両方同じ）
    float rms = 0.0f;
    float maxDelta = 0.0f;           // 隣り合うサンプルの差の最大（クリック・不連続の指標）
    std::uint32_t nonFinite = 0;
    std::uint32_t clipped = 0;
};

struct DeviceInfo {
    bool engineReady = false;
    bool hasDevice = false;
    std::string backend;
    std::string deviceName;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t periodFrames = 0;
    std::uint32_t periods = 0;
};

struct SoundSnapshot {
    std::string name;
    std::string path;
    std::string group;
    bool loaded = false;
    bool playing = false;
    bool atEnd = false;
    bool looping = false;
    bool preservePitch = false;
    bool registered = false;         // AudioServiceに登録済み（Workspace配下）
    float cursor = 0.0f;
    float length = 0.0f;
    float propertyVolume = 0.0f;     // Sound.Volume
    float appliedVolume = 0.0f;      // 実際にminiaudioへ設定されている音量
    float mixVolume = 0.0f;          // 直近のupdate3Dが計算した距離減衰後の音量
    float distance = 0.0f;           // 直近のupdate3Dでのリスナーまでの距離
    float pan = 0.0f;
    float pitch = 1.0f;
    float speed = 1.0f;
    bool spatialMixActive = false;
    float worldPosition[3] = {0.0f, 0.0f, 0.0f};
    float listenerPosition[3] = {0.0f, 0.0f, 0.0f};
    std::vector<std::string> ancestry;  // 祖先Spatialの名前とワールド位置（近い順）
};

struct SoundSample {
    double time = 0.0;
    float cursor = 0.0f;
    float appliedVolume = 0.0f;
    float mixVolume = 0.0f;
    float distance = 0.0f;
    float pan = 0.0f;
    bool playing = false;
    bool atEnd = false;
};

struct SoundEvent {
    double time = 0.0;
    std::string sound;
    std::string call;                // "Play" / "Stop" / "Reset" / "Seek"
    bool playingBefore = false;
    bool playingAfter = false;
    bool atEndBefore = false;
    float cursorBefore = 0.0f;
    float cursorAfter = 0.0f;
    float appliedVolume = 0.0f;
};

struct OutputStats {
    std::uint64_t blocks = 0;
    std::uint64_t nonFiniteBlocks = 0;
    std::uint64_t clippedBlocks = 0;
    std::uint64_t lateCallbacks = 0; // 期待間隔の1.5倍を超えたコールバック
    std::uint64_t silentBlocks = 0;
    double maxInterval = 0.0;
    float maxPeak = 0.0f;
    float maxDelta = 0.0f;
};

struct WaveformAnalysis {
    bool ok = false;
    std::string error;
    std::string path;
    double duration = 0.0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    float dcOffset = 0.0f;
    float firstSample = 0.0f;
    float lastSample = 0.0f;
    float leadingSilenceMs = 0.0f;   // 先頭の|x|<0.01が続く長さ
    std::vector<float> envelopeMin;  // 描画用（均等分割）
    std::vector<float> envelopeMax;
    std::vector<float> mono;         // モノラル化した全サンプル（切断位置の解析用）
};

// 一定間隔で巻き戻す再生（銃の連射）で、切断位置の波形の値。
// value が大きいほど、巻き戻した瞬間に不連続（クリック）が大きい。
struct CutPoint {
    double time = 0.0;
    float value = 0.0f;
};

} // namespace AudioDiag

class AudioDiagnostics {
public:
    struct Settings {
        bool bypassSpatial = false;  // trueならupdate3Dの距離減衰・パンを使わず、Volumeをそのまま設定する
    };

    static AudioDiagnostics& get();

    // タップをエンジンへ差し込む/外す（AudioServiceが呼ぶ）。
    void installTap(ma_engine& engine, ma_sound_group& sfx, ma_sound_group& bgm);
    void removeTap();
    bool tapInstalled() const { return m_tap != nullptr; }

    // 計測の有効/無効（無効時はイベント記録やサンプリングをしない）。
    void setEnabled(bool enabled) { m_enabled.store(enabled, std::memory_order_relaxed); }
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }
    Settings& settings() { return m_settings; }

    static double now();

    // メインスレッド: 毎フレーム呼ぶ。タップのブロックを取り込み、Soundの状態をサンプリングする。
    void pump(const std::vector<std::shared_ptr<Sound>>& sounds);
    void clear();

    // Soundの呼び出し記録（Sound側のトレースから）
    void recordEvent(AudioDiag::SoundEvent event);

    // 結果の参照（メインスレッド）
    const std::vector<AudioDiag::BlockStat>& blocks() const { return m_blocks; }
    const AudioDiag::OutputStats& stats() const { return m_stats; }
    const std::vector<AudioDiag::SoundEvent>& events() const { return m_events; }
    const std::map<std::string, std::vector<AudioDiag::SoundSample>>& soundSeries() const { return m_series; }
    const std::map<std::string, AudioDiag::SoundSnapshot>& latestSnapshots() const { return m_snapshots; }
    AudioDiag::DeviceInfo deviceInfo() const;
    double expectedBlockInterval() const;

    // 全体の要約（Markdown）。調査結果の共有用。
    std::string buildReport() const;
    // 時系列のCSV（出力ブロックとSound状態）。
    std::string buildCsv() const;

    static AudioDiag::WaveformAnalysis analyzeWaveform(const std::string& path);
    // mono波形を interval 秒ごとに巻き戻したとき、各切断位置の波形の値。
    static std::vector<AudioDiag::CutPoint> analyzeCuts(const AudioDiag::WaveformAnalysis& analysis,
                                                        double intervalSeconds);

    AudioDiagnostics();
    ~AudioDiagnostics();

private:
    struct Tap;
    static constexpr std::size_t RING_SIZE = 8192;
    static constexpr std::size_t MAX_UI_BLOCKS = 16384;
    static constexpr std::size_t MAX_SERIES = 2400;
    static constexpr std::size_t MAX_EVENTS = 2000;

    ma_engine* m_engine = nullptr;
    std::unique_ptr<Tap> m_tap;
    std::atomic<bool> m_enabled{false};
    Settings m_settings;

    // オーディオスレッド -> メインスレッドのリングバッファ（書き手1・読み手1）
    std::array<AudioDiag::BlockStat, RING_SIZE> m_ring{};
    std::atomic<std::uint64_t> m_head{0};
    std::uint64_t m_tail = 0;
    double m_lastBlockTime = 0.0;

    std::vector<AudioDiag::BlockStat> m_blocks;
    AudioDiag::OutputStats m_stats;
    std::vector<AudioDiag::SoundEvent> m_events;
    std::map<std::string, std::vector<AudioDiag::SoundSample>> m_series;
    std::map<std::string, AudioDiag::SoundSnapshot> m_snapshots;

    void drainRing();
    void account(const AudioDiag::BlockStat& block);
};
