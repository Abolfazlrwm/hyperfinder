#ifndef HYPERFINDER_THREAD_POOL_H
#define HYPERFINDER_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <type_traits>
#include <cstddef>
#include <stdexcept>

namespace hyperfinder {

// Fixed-size pool of worker threads executing queued tasks.
//
// The pool counts tasks that are queued or currently running. A task is
// counted before it is pushed to the queue and uncounted after it returns,
// so a task that enqueues further tasks keeps the pool "busy" for as long as
// its descendants are pending. waitIdle() blocks on that counter, which makes
// it safe to wait for a whole tree of recursively spawned work.
//
// waitIdle() must not be called from inside a task running on this pool: the
// calling worker would block forever waiting for its own completion.
class ThreadPool {
public:
    // thread_count == 0 is treated as 1: a pool without workers would accept
    // tasks that never run and make waitIdle() block forever.
    explicit ThreadPool(size_t threads = defaultThreadCount());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    // Blocks until every queued task has been executed and no task is running.
    void waitIdle();

    // Number of worker threads created by the constructor (always >= 1).
    std::size_t threadCount() const noexcept;

    // std::thread::hardware_concurrency() is allowed to return 0 when the
    // number of CPUs cannot be determined; fall back to a single worker.
    static std::size_t defaultThreadCount() noexcept;

private:
    void workerLoop();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;        // signalled when work is queued
    std::condition_variable idle_condition;   // signalled when the pool drains
    std::size_t active_tasks = 0;             // queued + currently running
    bool stop;
};

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
        ++active_tasks;
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

}  // namespace hyperfinder

#endif  // HYPERFINDER_THREAD_POOL_H
