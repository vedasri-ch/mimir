#include <gtest/gtest.h>
#include "../src/threadpool/thread_pool.hpp"

#include <atomic>
#include <vector>

using namespace mimir;

TEST(ThreadPoolTest, BasicExecution) {
    ThreadPool pool(4);
    auto f = pool.enqueue([] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

TEST(ThreadPoolTest, ConcurrentTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futs;

    for (int i = 0; i < 100; ++i)
        futs.push_back(pool.enqueue([&] { counter.fetch_add(1); }));

    for (auto& f : futs) f.get();
    EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPoolTest, ReturnValues) {
    ThreadPool pool(2);
    auto a = pool.enqueue([] { return std::string("hello"); });
    auto b = pool.enqueue([] { return std::string("world"); });
    EXPECT_EQ(a.get(), "hello");
    EXPECT_EQ(b.get(), "world");
}
