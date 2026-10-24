#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <functional>
#include <thread>

#include <bitloop/util/thread_queue.h>

TEST_CASE("ThreadQueue blocking drain finishes nested cleanup on the owner thread", "[thread_queue]")
{
    ThreadQueue queue;
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<bool> finished = false;
    bool owner_only = true;
    int completed = 0;
    int completed_before_return = 0;

    std::function<void()> cleanup = [&]()
    {
        owner_only &= std::this_thread::get_id() == owner;
        if (++completed < 80)
            queue.post(cleanup);
    };

    std::thread worker([&]()
    {
        queue.post(cleanup);
        queue.invokeBlocking([&]()
        {
            queue.drain();
            completed_before_return = completed;
        });
        finished.store(true, std::memory_order_release);
    });

    while (!finished.load(std::memory_order_acquire))
    {
        queue.pump();
        std::this_thread::yield();
    }
    worker.join();

    CHECK(owner_only);
    CHECK(completed_before_return == 80);
}
