#include <Core/ProcessWatchdog.hpp>

#include <algorithm>
#include <utility>

ProcessWatchdog::ProcessWatchdog(std::chrono::milliseconds timeout)
    : m_timeout(std::max(timeout, std::chrono::milliseconds(1))) {}

ProcessWatchdog::~ProcessWatchdog() {
    stop();
}

void ProcessWatchdog::start(TimeoutCallback callback) {
    if (m_active.exchange(true, std::memory_order_acq_rel)) return;

    if (m_thread.joinable()) m_thread.join();
    m_callback = std::move(callback);
    m_timedOut.store(false, std::memory_order_release);
    m_stopRequested.store(false, std::memory_order_release);
    m_lastHeartbeatTick.store(
        std::chrono::steady_clock::now().time_since_epoch().count(),
        std::memory_order_release);
    m_thread = std::thread(&ProcessWatchdog::monitor, this);
}

void ProcessWatchdog::stop() {
    if (!m_active.exchange(false, std::memory_order_acq_rel)) {
        if (m_thread.joinable()) {
            if (m_thread.get_id() == std::this_thread::get_id()) m_thread.detach();
            else m_thread.join();
        }
        return;
    }
    m_stopRequested.store(true, std::memory_order_release);
    if (m_thread.joinable()) {
        if (m_thread.get_id() == std::this_thread::get_id()) m_thread.detach();
        else m_thread.join();
    }
}

void ProcessWatchdog::heartbeat() noexcept {
    if (!m_active.load(std::memory_order_acquire)) return;
    m_lastHeartbeatTick.store(
        std::chrono::steady_clock::now().time_since_epoch().count(),
        std::memory_order_release);
}

bool ProcessWatchdog::isActive() const noexcept {
    return m_active.load(std::memory_order_acquire);
}

bool ProcessWatchdog::timedOut() const noexcept {
    return m_timedOut.load(std::memory_order_acquire);
}

std::chrono::milliseconds ProcessWatchdog::timeout() const noexcept {
    return m_timeout;
}

void ProcessWatchdog::monitor() {
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (m_stopRequested.load(std::memory_order_acquire)) return;

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto last = std::chrono::steady_clock::duration(
            m_lastHeartbeatTick.load(std::memory_order_acquire));
        if (now - last >= m_timeout &&
            !m_timedOut.exchange(true, std::memory_order_acq_rel)) {
            if (m_callback) m_callback();
            return;
        }
    }
}
