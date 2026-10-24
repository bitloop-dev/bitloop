#pragma once
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadQueue
{
public:
    ThreadQueue()
        : owner_thread_id_(std::this_thread::get_id())
    {}

    bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner_thread_id_;
    }

    template <typename F>
    void post(F&& fn)
    {
        using Fn = std::decay_t<F>;
        Fn* heap_fn = new Fn(std::forward<F>(fn));

        Task t;
        t.object = heap_fn;
        t.run = &runCallable<Fn>;
        t.cleanup = &deleteCallable<Fn>;

        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(t);
    }

    template <typename T>
    void retire(T* ptr) noexcept
    {
        if (!ptr) return;

        Task t;
        t.object = ptr;
        t.run = &runDelete<T>;
        t.cleanup = nullptr;

        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(t);
    }

    template <typename F>
    decltype(auto) invokeBlocking(F&& fn)
    {
        if (isOwnerThread())
            return std::forward<F>(fn)();

        using Fn = std::decay_t<F>;
        using R = std::invoke_result_t<Fn&>;

        struct State
        {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::exception_ptr ep;
            std::conditional_t<std::is_void_v<R>, char, R> result{};
        };

        auto state = std::make_shared<State>();

        post([state, fn2 = Fn(std::forward<F>(fn))]() mutable noexcept
        {
            try
            {
                if constexpr (std::is_void_v<R>)
                {
                    fn2();
                }
                else
                {
                    state->result = fn2();
                }
            }
            catch (...)
            {
                state->ep = std::current_exception();
            }

            {
                std::lock_guard<std::mutex> lk(state->m);
                state->done = true;
            }
            state->cv.notify_one();
        });

        std::unique_lock<std::mutex> lk(state->m);
        state->cv.wait(lk, [&] { return state->done; });

        if (state->ep)
            std::rethrow_exception(state->ep);

        if constexpr (!std::is_void_v<R>)
            return std::move(state->result);
    }

    void pump() noexcept
    {
        assert(isOwnerThread());
        pumpOnce();
    }

    void drain() noexcept
    {
        assert(isOwnerThread());
        while (pumpOnce()) {}
    }

private:
    struct Task
    {
        void* object = nullptr;
        void (*run)(void*) noexcept = nullptr;
        void (*cleanup)(void*) noexcept = nullptr;
    };

    bool pumpOnce() noexcept
    {
        std::vector<Task> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (tasks_.empty())
                return false;
            local.swap(tasks_);
        }

        for (const Task& t : local)
        {
            t.run(t.object);
            if (t.cleanup)
                t.cleanup(t.object);
        }
        return true;
    }

    template <typename T>
    static void runDelete(void* p) noexcept
    {
        delete static_cast<T*>(p);
    }

    template <typename Fn>
    static void runCallable(void* p) noexcept
    {
        (*static_cast<Fn*>(p))();
    }

    template <typename Fn>
    static void deleteCallable(void* p) noexcept
    {
        delete static_cast<Fn*>(p);
    }

private:
    std::mutex mutex_;
    std::vector<Task> tasks_;
    std::thread::id owner_thread_id_;
};

template <typename T>
struct DeferredDelete
{
    ThreadQueue* queue = nullptr;

    void operator()(T* p) const noexcept
    {
        if (!p) return;

        if (queue)
            queue->retire(p);
        else
            delete p;
    }
};

template <typename T>
using deferred_unique_ptr = std::unique_ptr<T, DeferredDelete<T>>;

template <typename T, typename... Args>
[[nodiscard]] static deferred_unique_ptr<T> make_deferred_unique(ThreadQueue& q, Args&&... args)
{
    return deferred_unique_ptr<T>(
        new T(std::forward<Args>(args)...),
        DeferredDelete<T>{ &q }
    );
}
