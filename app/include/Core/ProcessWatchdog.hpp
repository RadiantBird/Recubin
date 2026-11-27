#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <cstdint>
#include <thread>

// Monitors a Process execution interval without taking locks from the
// watchdog thread. The owner must call heartbeat() from the engine frame loop.
class ProcessWatchdog {
public:
    using TimeoutCallback = std::function<void()>;

    explicit ProcessWatchdog(std::chrono::milliseconds timeout =
                                 std::chrono::milliseconds(2000));
    ~ProcessWatchdog();

    ProcessWatchdog(const ProcessWatchdog&) = delete;
    ProcessWatchdog& operator=(const ProcessWatchdog&) = delete;

    // Starts monitoring. Calling start while active is a no-op.
    void start(TimeoutCallback callback);
    // Stops monitoring and waits for the watchdog thread to leave.
    void stop();
    // Marks a completed engine frame. This operation is lock-free.
    void heartbeat() noexcept;

    bool isActive() const noexcept;
    bool timedOut() const noexcept;
    std::chrono::milliseconds timeout() const noexcept;

private:
    void monitor();

    std::chrono::milliseconds m_timeout;
    TimeoutCallback m_callback;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_timedOut{false};
    std::atomic<std::int64_t> m_lastHeartbeatTick{0};
    std::thread m_thread;
};
