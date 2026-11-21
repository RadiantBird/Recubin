#include "Util/ThreadPool.hpp"

#include <algorithm>

namespace {
// 描画スレッド・ドライバスレッド・OSの分を残すため、ワーカー数に上限を設ける。
constexpr unsigned MAX_WORKERS = 7;
}

ThreadPool& ThreadPool::get() {
    static ThreadPool instance;
    return instance;
}

ThreadPool::ThreadPool() {
    const unsigned hardware = std::thread::hardware_concurrency();
    const unsigned workers = hardware > 1 ? std::min(hardware - 1, MAX_WORKERS) : 0;
    m_workers.reserve(workers);
    for (unsigned i = 0; i < workers; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& worker : m_workers) worker.join();
}

void ThreadPool::workerLoop() {
    std::uint64_t seenGeneration = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || m_generation != seenGeneration; });
            if (m_stop) return;
            seenGeneration = m_generation;
        }
        runTasks(seenGeneration);
    }
}

void ThreadPool::runTasks(std::uint64_t generation) {
    for (;;) {
        const std::function<void(std::size_t)>* task = nullptr;
        std::size_t index = 0;
        {
            // 世代の確認とindexの取得を同じロックで行う。ジョブが完了して次のジョブが
            // 始まったあとに、古いtaskポインタで新しいindexを実行してしまうのを防ぐ。
            std::lock_guard<std::mutex> lock(m_mutex);
            if (generation != m_generation || m_next >= m_taskCount) return;
            index = m_next++;
            task = m_task;
        }
        (*task)(index);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (--m_pending == 0) m_done.notify_all();
        }
    }
}

void ThreadPool::parallelFor(std::size_t taskCount,
                             const std::function<void(std::size_t)>& task) {
    if (taskCount == 0) return;
    if (taskCount == 1 || m_workers.empty()) {
        for (std::size_t i = 0; i < taskCount; ++i) task(i);
        return;
    }

    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_task = &task;
        m_taskCount = taskCount;
        m_next = 0;
        m_pending = taskCount;
        generation = ++m_generation;
    }
    m_wake.notify_all();

    runTasks(generation);

    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [&] { return m_pending == 0; });
    m_task = nullptr;
    m_taskCount = 0;
}
