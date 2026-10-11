#include <Network/SnapshotClock.hpp>

#include <algorithm>
#include <cmath>

void SnapshotClock::reset() {
    *this = SnapshotClock{};
}

void SnapshotClock::noteTick(std::uint32_t tick) {
    if (m_hasTick && static_cast<std::int32_t>(tick - m_latestTick) <= 0) return;
    m_latestTick = tick;
    m_hasTick = true;
    m_ticksSinceTick = 0.0;
}

void SnapshotClock::advance(float dt, double delayTicks) {
    if (!m_hasTick) return;
    const double seconds = static_cast<double>((std::max)(dt, 0.0f));
    const double step = seconds * TICKS_PER_SECOND;
    const double follow = (std::min)(1.0, seconds * FOLLOW_RATE);
    m_ticksSinceTick += step;
    // 最新受信tickからの経過を足して相手の現在tickを見積もる(受信間隔の段差で時刻が跳ねないように)
    const double target = static_cast<double>(m_latestTick) + m_ticksSinceTick - delayTicks;
    if (!m_hasRenderTick || std::abs(target - m_renderTick) > SNAP_ERROR_TICKS) {
        m_renderTick = target;
        m_hasRenderTick = true;
    } else {
        m_renderTick += step;
        m_renderTick += (target - m_renderTick) * follow;
    }

    // step時刻は、stepごとに+1ずつ進めつつフレームの表示時刻へ緩やかに合わせる
    const double error = m_renderTick - m_stepTick;
    if (!m_hasStepTick || std::abs(error) > SNAP_ERROR_TICKS) {
        m_stepTick = m_renderTick;
        m_hasStepTick = true;
    } else {
        m_stepTick += error * follow;
    }
}

double SnapshotClock::beginStep() {
    m_stepTick += 1.0;
    return m_stepTick;
}
