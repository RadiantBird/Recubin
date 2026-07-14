#pragma once
#include <chrono>
#include <vector>

// フレーム時間の区間計測ユーティリティ。
// メインループの各区間(physics/luau/shadow/main/swap等)のCPU時間を累積し、
// 1秒ごとに平均を1行ログ出力する。
class FrameProfiler {
public:
    static FrameProfiler& get();

    void beginSection(const char* name);
    void endSection(const char* name);
    void addCount(const char* name, long long n);
    void endFrame(); // 毎フレーム、メインループ末尾で1回呼ぶ

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
        bool running = false;
    };
    struct Counter {
        const char* name;
        long long accum = 0;
    };

    Section* findSection(const char* name); // strcmpで線形探索、無ければ追加
    Counter* findCounter(const char* name);

    std::vector<Section> m_sections; // 登録順を保持（ログの列順になる）
    std::vector<Counter> m_counters;
    int m_frames = 0;
    bool m_hasWindowStart = false;
    std::chrono::steady_clock::time_point m_windowStart;
};
