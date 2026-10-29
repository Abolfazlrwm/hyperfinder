#include "thread_pool.h"

ThreadPool::ThreadPool(size_t threads) : stop(false) {
    if (threads == 0) threads = 1;  // a pool without workers would never drain
    workers.reserve(threads);
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this] { return stop || !tasks.empty(); });
            if (stop && tasks.empty()) return;
            task = std::move(tasks.front());
            tasks.pop();
        }

        // enqueue() wraps every callable in a std::packaged_task, so a task
        // that throws stores the exception in its future instead of throwing
        // here. No exception can escape this call.
        task();

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            --active_tasks;
            if (active_tasks == 0) idle_condition.notify_all();
        }
    }
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    idle_condition.wait(lock, [this] { return active_tasks == 0; });
}

std::size_t ThreadPool::threadCount() const noexcept {
    return workers.size();
}

std::size_t ThreadPool::defaultThreadCount() noexcept {
    const unsigned detected = std::thread::hardware_concurrency();
    return detected == 0 ? 1U : static_cast<std::size_t>(detected);
}
