#pragma once
#include <cstdint>

// 相手の物理tickで刻まれたスナップショットを補間表示するための時計。
// Client: Hostのtickを1つ追う。Host: 主導権を持つClientごとにそのClientのtickを追う。
// - 表示時刻(renderTick): フレームごとに進め、「推定した相手の現在tick - 補間遅延」へ緩やかに合わせる
// - step時刻(stepTick): 固定stepごとに+1し、フレームごとに表示時刻へ緩やかに合わせる
//   (キネマティック目標・速度追従は固定stepごとにこの時刻の補間姿勢を使う)
class SnapshotClock {
public:
    void reset();
    // 受信したtickを記録する(32bitの周回を考慮し、新しいものだけ)
    void noteTick(std::uint32_t tick);
    // フレームの経過dtで表示時刻とstep時刻を進める
    void advance(float dt, double delayTicks);
    // これからシミュレートするstepの後の時刻を返す(step時刻を+1する)
    double beginStep();

    bool hasTick() const { return m_hasTick; }
    bool hasRenderTick() const { return m_hasRenderTick; }
    bool hasStepTick() const { return m_hasStepTick; }
    double renderTick() const { return m_renderTick; }
    double stepTick() const { return m_stepTick; }

private:
    static constexpr double TICKS_PER_SECOND = 60.0;
    static constexpr double SNAP_ERROR_TICKS = 30.0; // これ以上ずれたら(初回・長い途絶)合わせ直す
    static constexpr double FOLLOW_RATE = 2.0;       // 1秒あたりに詰めるずれの割合

    std::uint32_t m_latestTick = 0;
    bool m_hasTick = false;
    double m_ticksSinceTick = 0.0; // 最新tick受信からの経過(tick単位)
    double m_renderTick = 0.0;
    bool m_hasRenderTick = false;
    double m_stepTick = 0.0;       // 直近にシミュレートしたstep後の時刻
    bool m_hasStepTick = false;
};
