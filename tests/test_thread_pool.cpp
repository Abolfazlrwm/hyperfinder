#include "doctest/doctest.h"
#include <hyperfinder/thread_pool.h>

#include <atomic>
#include <stdexcept>

using hyperfinder::ThreadPool;

TEST_CASE("ThreadPool: waitIdle waits for every queued task") {
    ThreadPool pool(4);
    std::atomic<int> completed{0};

    for (int i = 0; i < 2000; ++i) {
        pool.enqueue([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.waitIdle();

    CHECK(completed.load() == 2000);
}

TEST_CASE("ThreadPool: waitIdle waits for tasks spawned by tasks") {
    ThreadPool pool(4);
    std::atomic<int> completed{0};

    // A task that spawns a task that spawns a task. The counter must stay
    // non-zero while any descendant is still pending.
    pool.enqueue([&pool, &completed] {
        pool.enqueue([&pool, &completed] {
            pool.enqueue([&completed] { completed.fetch_add(1); });
        });
    });

    pool.waitIdle();

    CHECK(completed.load() == 1);
}

TEST_CASE("ThreadPool: a single worker drains nested tasks without deadlocking") {
    // The case that breaks a pool whose workers block on their own children.
    ThreadPool pool(1);
    std::atomic<int> completed{0};

    pool.enqueue([&pool, &completed] {
        for (int i = 0; i < 50; ++i) {
            pool.enqueue([&completed] { completed.fetch_add(1); });
        }
        completed.fetch_add(1);
    });

    pool.waitIdle();

    CHECK(completed.load() == 51);
}

TEST_CASE("ThreadPool: enqueue returns a future carrying the result") {
    ThreadPool pool(2);
    auto future = pool.enqueue([](int a, int b) { return a + b; }, 20, 22);

    pool.waitIdle();

    CHECK(future.get() == 42);
}

TEST_CASE("ThreadPool: a task exception is rethrown by future::get") {
    ThreadPool pool(2);
    auto future = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });

    CHECK_THROWS_AS(future.get(), std::runtime_error);
    pool.waitIdle();
}

TEST_CASE("ThreadPool: the pool keeps working after a task threw") {
    ThreadPool pool(2);
    auto failing = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });
    CHECK_THROWS(failing.get());

    auto after = pool.enqueue([] { return 7; });
    pool.waitIdle();

    CHECK(after.get() == 7);
}

TEST_CASE("ThreadPool: destroying an idle pool does not hang") {
    for (int i = 0; i < 20; ++i) {
        ThreadPool pool(4);
        pool.enqueue([] {});
        pool.waitIdle();
    }
    CHECK(true);  // reaching this point is the assertion
}

TEST_CASE("ThreadPool: the destructor drains queued work before joining") {
    std::atomic<int> completed{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 100; ++i) {
            pool.enqueue([&completed] { completed.fetch_add(1); });
        }
        // No waitIdle(): the destructor must finish the queue and join.
    }

    CHECK(completed.load() == 100);
}

TEST_CASE("ThreadPool: zero threads is treated as one worker") {
    ThreadPool pool(0);
    std::atomic<int> completed{0};

    for (int i = 0; i < 10; ++i) {
        pool.enqueue([&completed] { completed.fetch_add(1); });
    }
    pool.waitIdle();

    CHECK(pool.threadCount() == 1);
    CHECK(completed.load() == 10);
}

TEST_CASE("ThreadPool: defaultThreadCount never returns zero") {
    CHECK(ThreadPool::defaultThreadCount() >= 1);
}
