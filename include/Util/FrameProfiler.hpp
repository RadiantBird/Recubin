#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

// フレーム時間の区間計測ユーティリティ。
// メインループの各区間(physics/luau/render/shadow/main/swap等)のCPU時間を
// フレーム単位の固定長履歴と集計ウィンドウへ記録する。
class FrameProfiler {
public:
    static constexpr std::size_t HISTORY_CAPACITY = 240;

    struct SectionSnapshot {
        std::array<float, HISTORY_CAPACITY> samples{};
        std::size_t count = 0;
        float latestMs = 0.0f;
        float averageMs = 0.0f;
        float peakMs = 0.0f;
    };

    struct CounterSnapshot {
        std::array<long long, HISTORY_CAPACITY> samples{};
        std::size_t count = 0;
        long long latest = 0;
        double average = 0.0;
        long long peak = 0;
    };

    static FrameProfiler& get();

    void beginSection(const char* name);
    void endSection(const char* name);
    void addCount(const char* name, long long n);
    void endFrame(); // 毎フレーム、メインループ末尾で1回呼ぶ
    bool getSectionSnapshot(
        const char* name, SectionSnapshot& snapshot) const;
    bool getCounterSnapshot(
        const char* name, CounterSnapshot& snapshot) const;

    // RAIIガード（begin/endの書き忘れ防止）
    class Scope {
    public:
        explicit Scope(const char* name);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        const char* m_name;
    };

private:
    FrameProfiler() = default;

    struct Section {
        const char* name;
        std::chrono::steady_clock::time_point begin;
        double accumMs = 0.0; // 集計ウィンドウ内の累積
        double frameMs = 0.0;
        std::array<float, HISTORY_CAPACITY> history{};
        std::size_t historyCount = 0;
        std::size_t historyWriteIndex = 0;
        bool running = false;
    };
    struct Counter {
        const char* name;
        long long accum = 0;
        long long frameAccum = 0;
        std::array<long long, HISTORY_CAPACITY> history{};
        std::size_t historyCount = 0;
        std::size_t historyWriteIndex = 0;
    };

    Section* findSection(const char* name); // strcmpで線形探索、無ければ追加
    Counter* findCounter(const char* name);

    std::vector<Section> m_sections; // 登録順を保持（ログの列順になる）
    std::vector<Counter> m_counters;
    int m_frames = 0;
    bool m_hasWindowStart = false;
    std::chrono::steady_clock::time_point m_windowStart;
};
