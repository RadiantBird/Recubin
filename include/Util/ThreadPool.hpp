#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// 描画スレッド専用の小さな並列forプール。ワーカーは常駐し、parallelForは
// 呼び出しスレッドも実行に加わって全タスクの完了まで戻らない。
// 同時に複数のparallelForを呼んではいけない(描画スレッドからのみ呼ぶ)。
class ThreadPool {
public:
    static ThreadPool& get();

    // 0..taskCount-1 の各indexに対してtaskを1回ずつ呼ぶ。taskは例外を投げないこと。
    // ワーカーが無い環境、またはtaskCountが1以下のときは呼び出しスレッドで直列実行する。
    void parallelFor(std::size_t taskCount, const std::function<void(std::size_t)>& task);

    // 呼び出しスレッドを除いたワーカー数。
    std::size_t workerCount() const { return m_workers.size(); }

    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    ThreadPool();
    void workerLoop();
    // 指定世代のジョブからタスクを取れなくなるまで実行する。
    void runTasks(std::uint64_t generation);

    std::vector<std::thread> m_workers;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;
    // 以下はm_mutexで保護する。
    const std::function<void(std::size_t)>* m_task = nullptr;
    std::size_t m_taskCount = 0;
    std::size_t m_next = 0;     // 次に配るtask index
    std::size_t m_pending = 0;  // 未完了のtask数
    std::uint64_t m_generation = 0;
    bool m_stop = false;
};
