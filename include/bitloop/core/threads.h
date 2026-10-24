#pragma once

// Fix for MSVC mutex constexpr bug
//#define _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR

#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <memory>
#include <vector>
#include <type_traits>
#include <span>

#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/types.h>
#endif

#include "BS_thread_pool.hpp"
#include "moodycamel/concurrentqueue.h"

#define BL_BEGIN_NS namespace bl {
#define BL_END_NS   }

BL_BEGIN_NS

namespace Thread
{
    using moodycamel::ConcurrentQueue;
    using moodycamel::BlockingConcurrentQueue;

    inline int max_threads = 0;
    inline std::unique_ptr<BS::thread_pool<BS::tp::none>> _pool;

    [[nodiscard]] inline unsigned int idealThreadCount()
    {
        #ifdef BL_SIMULATED_DEVICE
        return BL_SIMULATED_DEVICE.threads;
        #endif

        // 1. Try the standard C++ hint
        unsigned int n = std::thread::hardware_concurrency();
        if (n) return (n-1);

        // 2. PlatformManager-specific fall-backs
        #if defined(__EMSCRIPTEN__)
            return static_cast<unsigned int>(emscripten_num_logical_cores());
        #elif defined(_WIN32)
            DWORD w = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            n = w ? static_cast<unsigned int>(w) : 1;
        #elif defined(_SC_NPROCESSORS_ONLN)
            long p = sysconf(_SC_NPROCESSORS_ONLN);
            n = p > 0 ? static_cast<unsigned int>(p) : 1;
        #else
            n = 1;           // last-ditch fall-back
        #endif
            
        return (n > 1) ? (n - 1) : 1;
    }

    inline unsigned int threadCount()
    {
        return (max_threads == 0) ? idealThreadCount() : max_threads;
    }

    inline void setMaxThreads(int c)
    {
        if (_pool)
            _pool.release();

        #ifdef BL_SIMULATED_DEVICE
        max_threads = std::min(c, BL_SIMULATED_DEVICE.threads);
        #else
        max_threads = c;
        #endif

        _pool = std::make_unique<BS::thread_pool<BS::tp::none>>(Thread::threadCount());
    }

    [[nodiscard]] inline BS::thread_pool<BS::tp::none>& pool()
    {
        if (!_pool) _pool = std::make_unique<BS::thread_pool<BS::tp::none>>(Thread::threadCount());
        return *_pool.get();
    }

    template<typename sizeT>
    [[nodiscard]] inline std::vector<std::pair<sizeT, sizeT>> splitRanges(sizeT totalSize, sizeT numParts)
    {
        std::vector<std::pair<sizeT, sizeT>> ranges;

        sizeT partSize = totalSize / numParts;
        sizeT remainder = totalSize % numParts;

        sizeT start = 0;
        for (sizeT i = 0; i < numParts; ++i)
        {
            sizeT thisPartSize = partSize + (i < remainder ? 1 : 0);
            ranges.emplace_back(start, start + thisPartSize);
            start += thisPartSize;
        }

        return ranges;
    }

    template<typename sizeT, typename VecT, typename RectT>
    requires requires(const VecT& v, const RectT& r)
    {
        { v.x }        -> std::convertible_to<sizeT>;
        { v.y }        -> std::convertible_to<sizeT>;
        { r.x() }      -> std::convertible_to<sizeT>;
        { r.y() }      -> std::convertible_to<sizeT>;
        { r.width() }  -> std::convertible_to<sizeT>;
        { r.height() } -> std::convertible_to<sizeT>;
    }
    [[nodiscard]] inline std::vector<std::pair<sizeT, sizeT>> splitRanges(VecT container, RectT rect, sizeT numParts)
    {
        std::vector<std::pair<sizeT, sizeT>> ranges;

        if (numParts == 0 || rect.width() <= 0 || rect.height() <= 0)
            return ranges;

        ranges.reserve(numParts);

        const sizeT stride = static_cast<sizeT>(container.x);
        const sizeT rowsPerPart = static_cast<sizeT>(rect.height()) / numParts;
        const sizeT remainder = static_cast<sizeT>(rect.height()) % numParts;

        sizeT y = static_cast<sizeT>(rect.y());
        const sizeT x = static_cast<sizeT>(rect.x());
        const sizeT width = static_cast<sizeT>(rect.width());

        for (sizeT i = 0; i < numParts; ++i)
        {
            const sizeT rows = rowsPerPart + (i < remainder ? 1 : 0);

            if (rows == 0)
            {
                ranges.emplace_back(0, 0);
                continue;
            }

            const sizeT start = y * stride + x;
            const sizeT end = (y + rows - 1) * stride + x + width;

            ranges.emplace_back(start, end);
            y += rows;
        }

        return ranges;
    }

    template<typename sizeT>
    [[nodiscard]] inline std::pair<sizeT, sizeT> splitRange(sizeT totalSize, sizeT numParts, sizeT blockIndex)
    {
        if (numParts == 0) throw std::invalid_argument("numParts must be > 0");
        if (blockIndex >= numParts) throw std::out_of_range("blockIndex out of range");

        const sizeT base = totalSize / numParts;
        const sizeT extra = totalSize % numParts;

        const bool hasExtra = blockIndex < extra;
        const sizeT size = base + (hasExtra ? 1 : 0);

        const sizeT start = hasExtra
            ? blockIndex * (base + 1)
            : extra * (base + 1) + (blockIndex - extra) * base;

        return { start, start + size };   // [start, end)
    }

    //template<typename T, typename Callback>
    //void forEachBatch(std::vector<T>& items, Callback&& callback, int thread_count = Thread::threadCount())
    //{
    //    int item_count = (int)items.size();
    //    std::vector<std::pair<int, int>> ranges = Thread::splitRanges<int>(item_count, thread_count);
    //
    //    std::vector<std::future<void>> futures(thread_count);
    //
    //    for (int ti = 0; ti < thread_count; ti++)
    //    {
    //        const std::pair<int, int>& range = ranges[ti];
    //        futures[ti] = Thread::pool().submit_task([&]()
    //        {
    //            int i0 = range.first;
    //            int i1 = range.second;
    //            callback(i0, i1);
    //        });
    //    }
    //
    //    for (int ti = 0; ti < thread_count; ti++)
    //    {
    //        if (futures[ti].valid())
    //            futures[ti].get();
    //    }
    //}

    template<typename SizeT, typename Callback>
        requires (std::invocable<Callback&, SizeT /*i0*/, SizeT /*i1*/>)
    auto forEachBatch(SizeT N,
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        using CB = std::decay_t<Callback>;
        static_assert(std::is_invocable_v<CB&, int, int>);
        using R = std::invoke_result_t<CB&, int, int>;
        using E = std::remove_cvref_t<R>;

        auto ranges = Thread::splitRanges<SizeT>(N, thread_count);

        CB cb = std::forward<Callback>(callback);

        if constexpr (std::is_void_v<R>) {
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, i0, i1] {
                    cb(i0, i1);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return; // void
        }
        else {
            std::vector<E> out(ranges.size());
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, i0, i1, &out, ti] {
                    out[ti] = cb(i0, i1);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return out; // std::vector<E>
        }
    }

    template<typename VecT, typename RectT, typename Callback>
        requires std::invocable<Callback&, int, int> &&
        requires(const VecT& v, const RectT& r)
    {
        { v.x }        -> std::convertible_to<int>;
        { v.y }        -> std::convertible_to<int>;
        { r.x() }      -> std::convertible_to<int>;
        { r.y() }      -> std::convertible_to<int>;
        { r.width() }  -> std::convertible_to<int>;
        { r.height() } -> std::convertible_to<int>;
    }
    auto forEachBatch(
        VecT container,
        RectT rect_inner,
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        using CB = std::decay_t<Callback>;
        static_assert(std::is_invocable_v<CB&, int, int>);
        using R = std::invoke_result_t<CB&, int, int>;
        using E = std::remove_cvref_t<R>;

        auto ranges = Thread::splitRanges<int>(container, rect_inner, thread_count);

        CB cb = std::forward<Callback>(callback);

        if constexpr (std::is_void_v<R>) {
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, i0, i1] {
                    cb(i0, i1);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return; // void
        }
        else {
            std::vector<E> out(ranges.size());
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, i0, i1, &out, ti] {
                    out[ti] = cb(i0, i1);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return out; // std::vector<E>
        }
    }

    template<typename T, typename Callback> requires (std::invocable<Callback&, int, int> && !std::invocable<Callback&, std::span<T>>)
    auto forEachBatch(std::vector<T>& items,
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        using CB = std::decay_t<Callback>;
        static_assert(std::is_invocable_v<CB&, int, int>);
        using R = std::invoke_result_t<CB&, int, int>;
        using E = std::remove_cvref_t<R>;

        const int N = static_cast<int>(items.size());
        auto ranges = Thread::splitRanges<int>(N, thread_count);

        CB cb = std::forward<Callback>(callback);

        if constexpr (std::is_void_v<R>) {
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, i0, i1] {
                    cb(i0, i1);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return; // void
        }
        else {
            std::vector<E> out(ranges.size());
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, i0, i1, &out, ti] {
                    out[ti] = cb(i0, i1);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return out; // std::vector<E>
        }
    }

    // ---------- span<T> (mutable) overload ----------
    // usage: Thread::forEachBatch(vec, [&](std::span<T> span) {  });
    // usage: Thread::forEachBatch(vec, [&](int i0, int i1) {  });
    template<typename T, typename Callback> requires std::invocable<Callback&, std::span<T>>
    auto forEachBatch(std::vector<T>& items,
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        using CB = std::decay_t<Callback>;
        using R = std::invoke_result_t<CB&, std::span<T>>;
        using E = std::remove_cvref_t<R>;

        const int N = static_cast<int>(items.size());
        auto ranges = Thread::splitRanges<int>(N, thread_count);

        CB cb = std::forward<Callback>(callback);

        if constexpr (std::is_void_v<R>) {
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, &items, i0, i1] {
                    cb(std::span<T>(items.data() + i0, static_cast<size_t>(i1 - i0)));
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return;
        }
        else {
            std::vector<E> out(ranges.size());
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, &items, i0, i1, &out, ti] {
                    out[ti] = cb(std::span<T>(items.data() + i0, static_cast<size_t>(i1 - i0)));
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return out;
        }
    }

    // ---------- span<T> + thread_index (mutable) overload ----------
    template<typename T, typename Callback> requires std::invocable<Callback&, std::span<T>, int>
    auto forEachBatch(std::vector<T>& items,
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        using CB = std::decay_t<Callback>;
        using R = std::invoke_result_t<CB&, std::span<T>, int>;
        using E = std::remove_cvref_t<R>;

        const int N = static_cast<int>(items.size());
        auto ranges = Thread::splitRanges<int>(N, thread_count);

        CB cb = std::forward<Callback>(callback);

        if constexpr (std::is_void_v<R>) {
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, &items, i0, i1, ti] {
                    cb(std::span<T>(items.data() + i0, static_cast<size_t>(i1 - i0)), (int)ti);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return;
        }
        else {
            std::vector<E> out(ranges.size());
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, &items, i0, i1, &out, ti] {
                    out[ti] = cb(std::span<T>(items.data() + i0, static_cast<size_t>(i1 - i0)), ti);
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return out;
        }
    }

    // ---------- span<const T> (read-only) overload ----------
    template<typename T, typename Callback> requires std::invocable<Callback&, std::span<const T>>
    auto forEachBatch(const std::vector<T>& items,
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        using CB = std::decay_t<Callback>;
        using R = std::invoke_result_t<CB&, std::span<const T>>;
        using E = std::remove_cvref_t<R>;

        const int N = static_cast<int>(items.size());
        auto ranges = Thread::splitRanges<int>(N, thread_count);

        CB cb = std::forward<Callback>(callback);

        if constexpr (std::is_void_v<R>) {
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, &items, i0, i1] {
                    cb(std::span<const T>(items.data() + i0, static_cast<size_t>(i1 - i0)));
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return;
        }
        else {
            std::vector<E> out(ranges.size());
            std::vector<std::future<void>> futs;
            futs.reserve(ranges.size());
            for (size_t ti = 0; ti < ranges.size(); ++ti) {
                auto [i0, i1] = ranges[ti];
                futs.emplace_back(Thread::pool().submit_task([&cb, &items, i0, i1, &out, ti] {
                    out[ti] = cb(std::span<const T>(items.data() + i0, static_cast<size_t>(i1 - i0)));
                }));
            }
            for (auto& f : futs) if (f.valid()) f.get();
            return out;
        }
    }
}

// todo: Move to new thread_sync.h
struct SharedSync
{
    std::atomic<bool> quitting{ false };
    std::atomic<bool> updating_live_buffer{ false };

    std::mutex live_buffer_mutex;
    std::mutex shadow_buffer_mutex;
    std::mutex state_mutex;

    std::condition_variable cv;
    std::condition_variable cv_updating_live_buffer;

    bool project_thread_started = false;
    bool frame_ready_to_draw = false;
    bool frame_consumed = false;

    std::atomic<bool> immediate_update{ false };

    void flag_ready_to_draw()
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        frame_ready_to_draw = true;
        frame_consumed = false;
    }

    void wait_until_gui_consumes_frame()
    {
        std::unique_lock<std::mutex> lock(state_mutex);
        cv.wait(lock, [this] { return frame_consumed || quitting.load(); });
    }

    void wait_until_live_buffer_updated()
    {
        std::unique_lock<std::mutex> live_lock(live_buffer_mutex);
        cv_updating_live_buffer.wait(live_lock, [this]() {
            return !updating_live_buffer.load();
        });
    }

    void quit()
    {
        quitting.store(true);
        cv.notify_all();
    }

    void setImmediateUpdate(bool b)
    {
        immediate_update.store(b, std::memory_order_release);
    }

    bool immediateUpdateRequested()
    {
        return immediate_update.load(std::memory_order_acquire);
    }
};

BL_END_NS
