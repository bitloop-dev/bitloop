#pragma once
#include <bitloop/core/world_object.h>
#include <bitloop/core/types.h>
#include <bitloop/core/threads.h>
#include <bitloop/core/camera.h>
#include <bitloop/util/math_util.h>

BL_BEGIN_NS;

struct TileBlock
{
    int tile_index;
    int x0, y0, x1, y1;
};

struct TileBlockProgress
{
    // build params (to detect changes)
    int bmp_w = 0, bmp_h = 0;
    int tile_w = 0, tile_h = 0;
    int block_w = 0, block_h = 0;
    int tiles_x = 0, tiles_y = 0;

    // one micro-block = one job
    std::vector<TileBlock> blocks;       // size = sum over tiles of ceil(tile_w/block_w)*ceil(tile_h/block_h)
    std::atomic<int>  next_block{ 0 };   // global cursor (persists across frames)

    // per-tile progress
    std::vector<uint32_t> blocks_total_per_tile;
    std::vector<uint32_t> blocks_done_per_tile; // incremented via atomic_ref

    int owner_count = 0;            // threads used when plan was initialized
    std::vector<int> owner_cursor;  // blocks consumed per owner (size = owner_count)

    // call this when blocks (or thread_count) may have changed
    void ensure_owner_slots(int threads) {
        if (owner_count != threads || owner_cursor.size() != (size_t)threads) {
            owner_count = threads;
            owner_cursor.assign(threads, 0); // reset resume state (mapping changed)
        }
    }

    // reset after completion
    void reset_progress_only() {
        std::fill(owner_cursor.begin(), owner_cursor.end(), 0);
    }

    bool finished()
    {
        const int N = static_cast<int>(blocks.size());
        bool ret = true;
        for (int k = 0; k < owner_count; ++k) {
            const int bi = k + owner_cursor[k] * owner_count;
            if (bi < N) { ret = false; break; }
        }
        return ret;
    }
};

namespace detail
{
    inline double now_ms()
    {
        #ifdef __EMSCRIPTEN__
        return emscripten_get_now(); // high-res ms
        #else
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
        #endif
    }

    // build/rebuild micro-blocks. Returns true if rebuilt.
    inline bool ensureBlocksBuilt(TileBlockProgress& P,
        int bmp_w, int bmp_h,
        int tile_w, int tile_h,
        int block_w, int block_h)
    {
        if (block_w <= 0) block_w = 64;
        if (block_h <= 0) block_h = 8;

        const bool changed =
            (P.bmp_w != bmp_w) || (P.bmp_h != bmp_h) ||
            (P.tile_w != tile_w) || (P.tile_h != tile_h) ||
            (P.block_w != block_w) || (P.block_h != block_h);

        if (!changed) return false;

        P.bmp_w = bmp_w;  P.bmp_h = bmp_h;
        P.tile_w = tile_w; P.tile_h = tile_h;
        P.block_w = block_w; P.block_h = block_h;

        P.tiles_x = (bmp_w + tile_w - 1) / tile_w;
        P.tiles_y = (bmp_h + tile_h - 1) / tile_h;
        const int tile_count = P.tiles_x * P.tiles_y;

        std::vector<TileBlock> blocks;
        blocks.reserve((bmp_w / block_w + 1) * (bmp_h / block_h + 1));

        std::vector<uint32_t> total(tile_count, 0), done(tile_count, 0);

        for (int ty = 0; ty < P.tiles_y; ++ty) {
            for (int tx = 0; tx < P.tiles_x; ++tx) {
                const int tile_index = ty * P.tiles_x + tx;

                const int x0 = tx * tile_w;
                const int y0 = ty * tile_h;
                const int x1 = std::min(x0 + tile_w, bmp_w);
                const int y1 = std::min(y0 + tile_h, bmp_h);

                for (int by = y0; by < y1; by += block_h) {
                    const int yy1 = std::min(by + block_h, y1);
                    for (int bx = x0; bx < x1; bx += block_w) {
                        const int xx1 = std::min(bx + block_w, x1);
                        blocks.push_back(TileBlock{ tile_index, bx, by, xx1, yy1 });
                        ++total[tile_index];
                    }
                }
            }
        }

        P.blocks = std::move(blocks);
        P.blocks_total_per_tile = std::move(total);
        P.blocks_done_per_tile = std::move(done);
        P.next_block.store(0, std::memory_order_relaxed);
        return true;
    }
}

// w/h info for both 'Image' and 'RasterGrid' (diamond inheritance)
struct RasterGrid
{
    union
    {
        struct { int raster_w; int raster_h; };
        IVec2 raster_size;
    };

    RasterGrid(int w = 0, int h = 0)
        : raster_w(w), raster_h(h) {}

    int rasterWidth() const { return raster_w; }
    int rasterHeight() const { return raster_h; }
    uint32_t rasterCount() const { return (uint32_t)raster_w * (uint32_t)raster_h; }

    virtual void setRasterSize(int w, int h)
    {
        assert(w >= 0 && h >= 0);
        raster_w = w;
        raster_h = h;
    }

    template<typename Callback>
    bool forEachPixel(
        int& current_row,
        Callback&& callback,
        int thread_count = Thread::threadCount(),
        int timeout_ms = 0)
    {
        using CallbackT = std::decay_t<Callback>;
        using Ret = std::invoke_result_t<CallbackT&, int, int>;

        static_assert(std::is_void_v<Ret> || std::is_same_v<std::remove_cvref_t<Ret>, bool>,
            "Callback must be: void(int x, int y) or bool(int x, int y)");

        CallbackT cb = std::forward<Callback>(callback);

        auto timeout = timeout_ms ?
            std::chrono::milliseconds{ timeout_ms } :
            std::chrono::steady_clock::duration::max();

        if constexpr (std::is_void_v<Ret>)
        {
            if (thread_count > 0)
            {
                auto start_time = std::chrono::steady_clock::now();

                std::vector<std::future<void>> futures(thread_count);
                std::vector<std::atomic<bool>> active_threads(thread_count);

                for (int ti = 0; ti < thread_count; ti++)
                    active_threads[ti].store(false, std::memory_order_relaxed);

                std::atomic<bool> timed_out{ false };

                while (!timed_out.load(std::memory_order_relaxed))
                {
                    for (int ti = 0; ti < thread_count; ti++)
                    {
                        if (timed_out.load(std::memory_order_relaxed))
                            break;

                        if (active_threads[ti].load(std::memory_order_relaxed))
                            continue;

                        const int thread_index = ti;
                        const int row = current_row++;

                        if (row >= raster_h)
                        {
                            timed_out.store(true, std::memory_order_relaxed);
                            break;
                        }

                        active_threads[ti].store(true, std::memory_order_relaxed);

                        futures[ti] = Thread::pool().submit_task([&, row, thread_index]()
                        {
                            for (int bmp_x = 0; bmp_x < raster_w; ++bmp_x)
                                std::invoke(cb, bmp_x, row);

                            if (std::chrono::steady_clock::now() - start_time >= timeout)
                            {
                                timed_out.store(true, std::memory_order_relaxed);
                            }
                            else
                            {
                                active_threads[thread_index].store(false, std::memory_order_relaxed);
                            }
                        });
                    }

                    std::this_thread::yield();
                }

                for (int ti = 0; ti < thread_count; ti++)
                {
                    if (futures[ti].valid())
                        futures[ti].get();
                }
            }
            else
            {
                for (int bmp_y = 0; bmp_y < raster_h; ++bmp_y)
                {
                    for (int bmp_x = 0; bmp_x < raster_w; ++bmp_x)
                        std::invoke(cb, bmp_x, bmp_y);
                }
            }

            if (current_row >= raster_h)
            {
                current_row = 0;
                return true;
            }
            return false;
        }
        else
        {
            std::atomic<bool> stop_requested{ false };

            if (thread_count > 0)
            {
                auto start_time = std::chrono::steady_clock::now();

                std::vector<std::future<void>> futures(thread_count);
                std::vector<std::atomic<bool>> active_threads(thread_count);

                for (int ti = 0; ti < thread_count; ti++)
                    active_threads[ti].store(false, std::memory_order_relaxed);

                std::atomic<bool> timed_out{ false };

                while (!timed_out.load(std::memory_order_relaxed) &&
                    !stop_requested.load(std::memory_order_relaxed))
                {
                    for (int ti = 0; ti < thread_count; ti++)
                    {
                        if (timed_out.load(std::memory_order_relaxed) ||
                            stop_requested.load(std::memory_order_relaxed))
                            break;

                        if (active_threads[ti].load(std::memory_order_relaxed))
                            continue;

                        const int thread_index = ti;
                        const int row = current_row++;

                        if (row >= raster_h)
                        {
                            timed_out.store(true, std::memory_order_relaxed);
                            break;
                        }

                        active_threads[ti].store(true, std::memory_order_relaxed);

                        futures[ti] = Thread::pool().submit_task([&, row, thread_index]()
                        {
                            for (int bmp_x = 0; bmp_x < raster_w; ++bmp_x)
                            {
                                if (stop_requested.load(std::memory_order_relaxed))
                                    break;

                                if (std::invoke(cb, bmp_x, row))
                                {
                                    stop_requested.store(true, std::memory_order_relaxed);
                                    break;
                                }
                            }

                            active_threads[thread_index].store(false, std::memory_order_relaxed);

                            if (!stop_requested.load(std::memory_order_relaxed) &&
                                std::chrono::steady_clock::now() - start_time >= timeout)
                            {
                                timed_out.store(true, std::memory_order_relaxed);
                            }
                        });
                    }

                    std::this_thread::yield();
                }

                for (int ti = 0; ti < thread_count; ti++)
                {
                    if (futures[ti].valid())
                        futures[ti].get();
                }
            }
            else
            {
                for (int bmp_y = 0; bmp_y < raster_h; ++bmp_y)
                {
                    for (int bmp_x = 0; bmp_x < raster_w; ++bmp_x)
                    {
                        if (std::invoke(cb, bmp_x, bmp_y))
                        {
                            stop_requested.store(true, std::memory_order_relaxed);
                            break;
                        }
                    }

                    if (stop_requested.load(std::memory_order_relaxed))
                        break;
                }
            }

            if (stop_requested.load(std::memory_order_relaxed))
                return false;

            if (current_row >= raster_h)
            {
                current_row = 0;
                return true;
            }
            return false;
        }
    }


    template<typename Callback>
    void forEachPixel(
        Callback&& callback,
        int thread_count = Thread::threadCount())
    {
        int row = 0;
        forEachPixel(
            row,
            callback,
            thread_count,
            0
        );
    }
};

template<typename T=f64>
class WorldRasterGridT : public WorldObjectT<T>, public virtual RasterGrid
{
public:

    // todo: p could be a lower-precision world coord
    [[nodiscard]] IVec2 pixelPosFromWorld(Vec2<T> p)
    {
        return static_cast<IVec2>(WorldObjectT<T>::worldToUVRatio(p) * raster_size);
    }

    // slow to call per-pixel, prefer forEachPixel with a callback instead
    template<typename WorldT>
    void pixelWorldPos(int px, int py, WorldT& wx, WorldT& wy)
    {
        Quad<WorldT> world_quad = static_cast<Quad<WorldT>>(WorldObjectT<T>::worldQuad());
        WorldT ax = world_quad.a.x, ay = world_quad.a.y;
        WorldT bx = world_quad.b.x, by = world_quad.b.y;
        WorldT cx = world_quad.c.x, cy = world_quad.c.y;
        WorldT dx = world_quad.d.x, dy = world_quad.d.y;

        WorldT t_bmp_w = static_cast<WorldT>(raster_w);
        WorldT t_bmp_h = static_cast<WorldT>(raster_h);

        WorldT bmp_fx = static_cast<WorldT>(px) + WorldT{ 0.5 };
        WorldT bmp_fy = static_cast<WorldT>(py) + WorldT{ 0.5 };

        WorldT _u = bmp_fx / t_bmp_w;
        WorldT _v = bmp_fy / t_bmp_h;

        WorldT scan_left_x = ax + (dx - ax) * _v;
        WorldT scan_left_y = ay + (dy - ay) * _v;
        WorldT scan_right_x = bx + (cx - bx) * _v;
        WorldT scan_right_y = by + (cy - by) * _v;

        wx = scan_left_x + (scan_right_x - scan_left_x) * _u;
        wy = scan_left_y + (scan_right_y - scan_left_y) * _u;
    }

    template<typename WorldT = T, typename Callback>
    bool forEachWorldPixel(
        int& current_row,
        Callback&& callback,
        int thread_count = Thread::threadCount(),
        int timeout_ms = 0,
        std::atomic<bool>* busy = nullptr
    )
    {
        auto timeout = timeout_ms ?
            std::chrono::milliseconds{ timeout_ms } :
            std::chrono::steady_clock::duration::max();

        // World quad might be higher precision than is requested for the current zoom level, downgrade to requested WorldT
        Quad<WorldT> world_quad = static_cast<Quad<WorldT>>(WorldObjectT<T>::worldQuad());

        WorldT ax = world_quad.a.x, ay = world_quad.a.y;
        WorldT bx = world_quad.b.x, by = world_quad.b.y;
        WorldT cx = world_quad.c.x, cy = world_quad.c.y;
        WorldT dx = world_quad.d.x, dy = world_quad.d.y;

        WorldT t_bmp_w = static_cast<WorldT>(raster_w);
        WorldT t_bmp_h = static_cast<WorldT>(raster_h);

        if (thread_count > 0)
        {
            if (busy)
            {
                for (int i = 0; i < thread_count; ++i)
                    busy[i].store(false, std::memory_order_relaxed);
            }

            auto start_time = std::chrono::steady_clock::now();

            std::vector<std::future<void>> futures(thread_count);
            std::vector<std::atomic<bool>> active_threads(thread_count);

            for (int ti = 0; ti < thread_count; ti++)
                active_threads[ti].store(false);

            std::atomic<bool> timed_out{ false };

            // Continuously spin checking for idle threads and scheduling new rows...
            while (!timed_out.load(std::memory_order_relaxed))
            {
                for (int ti = 0; ti < thread_count; ti++)
                {
                    if (timed_out.load(std::memory_order_relaxed))
                        break;

                    if (active_threads[ti].load(std::memory_order_relaxed))
                        continue;

                    // Found an idle thread...

                    const int thread_index = ti;
                    const int row = current_row++;

                    if (row >= raster_h)
                    {
                        timed_out.store(true);
                        break;
                    }

                    active_threads[thread_index].store(true, std::memory_order_relaxed);
                    if (busy) busy[thread_index].store(true, std::memory_order_relaxed);

                    futures[ti] = Thread::pool().submit_task([&, row, thread_index]()
                    {
                        // Interpolate row pixel coordinate and invoke callback
                        WorldT bmp_fx, bmp_fy = static_cast<WorldT>(row) + WorldT{ 0.5 };
                        WorldT _v = bmp_fy / t_bmp_h;
                        WorldT scan_left_x = ax + (dx - ax) * _v;
                        WorldT scan_left_y = ay + (dy - ay) * _v;
                        WorldT scan_right_x = bx + (cx - bx) * _v;
                        WorldT scan_right_y = by + (cy - by) * _v;
                        for (int bmp_x = 0; bmp_x < raster_w; ++bmp_x)
                        {
                            bmp_fx = static_cast<WorldT>(bmp_x) + WorldT{ 0.5 };
                            WorldT _u = bmp_fx / t_bmp_w;
                            WorldT wx = scan_left_x + (scan_right_x - scan_left_x) * _u;
                            WorldT wy = scan_left_y + (scan_right_y - scan_left_y) * _u;

                            if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT, int>)
                                std::forward<Callback>(callback)(bmp_x, row, wx, wy, thread_index);
                            else if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT>)
                                std::forward<Callback>(callback)(bmp_x, row, wx, wy);
                            else
                                static_assert(sizeof(Callback) == 0,
                                "Callback must be: void( int x, int y, float_t wx, float_y wy, [[optional]] int thread_index)");
                        }

                        // After each row solved, check if timeout exceeded
                        if (std::chrono::steady_clock::now() - start_time >= timeout)
                        {
                            // If so, signal that we shouldn't pick up any new tasks - break out the loop
                            timed_out.store(true);
                        }
                        else
                        {
                            // Otherwise, schedule the next available row
                            active_threads[thread_index].store(false);
                            if (busy) busy[thread_index].store(false, std::memory_order_relaxed);
                        }
                    });
                }

                std::this_thread::yield();
            }

            // After timing out, wait for remaining threads to finalize
            for (int ti = 0; ti < thread_count; ti++)
            {
                if (futures[ti].valid())
                    futures[ti].get();
            }
        }
        else
        {
            WorldT bmp_fx, bmp_fy;
            for (int bmp_y = 0; bmp_y < raster_h; ++bmp_y)
            {
                // Interpolate left and right edges of the scanline
                bmp_fy = static_cast<WorldT>(bmp_y) + WorldT{ 0.5 };
                WorldT _v = bmp_fy / t_bmp_h;
                WorldT scan_left_x = ax + (dx - ax) * _v;
                WorldT scan_left_y = ay + (dy - ay) * _v;
                WorldT scan_right_x = bx + (cx - bx) * _v;
                WorldT scan_right_y = by + (cy - by) * _v;
                for (int bmp_x = 0; bmp_x < raster_w; ++bmp_x)
                {
                    bmp_fx = static_cast<WorldT>(bmp_x) + WorldT{ 0.5 };
                    WorldT _u = bmp_fx / t_bmp_w;
                    WorldT wx = scan_left_x + (scan_right_x - scan_left_x) * _u;
                    WorldT wy = scan_left_y + (scan_right_y - scan_left_y) * _u;

                    if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT, int>)
                        std::forward<Callback>(callback)(bmp_x, bmp_y, wx, wy, 0);
                    else if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT>)
                        std::forward<Callback>(callback)(bmp_x, bmp_y, wx, wy);
                    else
                        static_assert(sizeof(Callback) == 0,
                        "Callback must be: void( int x, int y, float_t wx, float_y wy, [[optional]] int thread_index)");
                }
            }
        }

        if (current_row >= raster_h)
        {
            current_row = 0;
            return true;
        }
        return false;
    }

    template<typename WorldT = T, typename Callback>
    void forEachWorldPixel(
        Callback&& callback,
        int thread_count = Thread::threadCount()
    )
    {
        int current_row = 0;
        forEachWorldPixel<WorldT>(current_row, callback, thread_count);
    }

    template<typename WorldT, typename Callback>
    bool forEachWorldTile(
        int tile_w, int tile_h,
        Callback&& callback,
        int thread_count = Thread::threadCount()
    )
    {
        int current_tile = 0;

        // World quad might be higher precision than is requested for the current zoom level, downgrade to requested WorldT
        Quad<WorldT> world_quad = static_cast<Quad<WorldT>>(WorldObjectT<T>::worldQuad());

        WorldT ax = world_quad.a.x, ay = world_quad.a.y;
        WorldT bx = world_quad.b.x, by = world_quad.b.y;
        WorldT cx = world_quad.c.x, cy = world_quad.c.y;
        WorldT dx = world_quad.d.x, dy = world_quad.d.y;

        WorldT t_bmp_w = static_cast<WorldT>(raster_w);
        WorldT t_bmp_h = static_cast<WorldT>(raster_h);

        const int tiles_x = (raster_w + tile_w - 1) / tile_w;
        const int tiles_y = (raster_h + tile_h - 1) / tile_h;
        const int tile_count = tiles_x * tiles_y;

        auto start_time = std::chrono::steady_clock::now();

        std::vector<std::future<void>> futures(thread_count);
        std::vector<std::atomic<bool>> active_threads(thread_count);
        for (int ti = 0; ti < thread_count; ++ti)
            active_threads[ti].store(false, std::memory_order_relaxed);

        std::atomic<bool> timed_out{ false };

        // Continuously spin checking for idle threads and scheduling new tiles...
        while (!timed_out.load(std::memory_order_relaxed))
        {
            for (int ti = 0; ti < thread_count; ++ti)
            {
                if (timed_out.load(std::memory_order_relaxed))
                    break;

                if (active_threads[ti].load(std::memory_order_relaxed))
                    continue;

                // Found an idle thread...
                const int thread_index = ti;
                const int tile_index = current_tile++;

                if (tile_index >= tile_count)
                {
                    timed_out.store(true, std::memory_order_relaxed);
                    break;
                }

                const int tx = tile_index % tiles_x;
                const int ty = tile_index / tiles_x;

                const int x0 = tx * tile_w;
                const int y0 = ty * tile_h;
                const int x1 = std::min(x0 + tile_w, raster_w);
                const int y1 = std::min(y0 + tile_h, raster_h);

                active_threads[thread_index].store(true, std::memory_order_relaxed);

                futures[ti] = Thread::pool().submit_task([&, x0, y0, x1, y1, thread_index, tile_index]()
                {
                    const int px = x0 + (x1 - x0 - 1) / 2;
                    const int py = y0 + (y1 - y0 - 1) / 2;

                    // Pixel-center sampling
                    const WorldT bmp_fx = static_cast<WorldT>(px) + WorldT{ 0.5 };
                    const WorldT bmp_fy = static_cast<WorldT>(py) + WorldT{ 0.5 };

                    // Interpolate world coords along the two vertical edges at this scanline
                    const WorldT v = bmp_fy / t_bmp_h;
                    const WorldT scan_left_x  = ax + (dx - ax) * v;
                    const WorldT scan_left_y  = ay + (dy - ay) * v;
                    const WorldT scan_right_x = bx + (cx - bx) * v;
                    const WorldT scan_right_y = by + (cy - by) * v;

                    // Interpolate across the scanline
                    const WorldT u = bmp_fx / t_bmp_w;
                    const WorldT wx = scan_left_x + (scan_right_x - scan_left_x) * u;
                    const WorldT wy = scan_left_y + (scan_right_y - scan_left_y) * u;

                    if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT, int, int, int, int, int>)
                    {
                        std::forward<Callback>(callback)(px, py, wx, wy, tile_index, x0, y0, x1, y1);
                    }
                    else
                    {
                        std::forward<Callback>(callback)(px, py, wx, wy, tile_index);
                    }

                    // Allow this worker to grab another tile
                    active_threads[thread_index].store(false, std::memory_order_relaxed);
                });
            }

            std::this_thread::yield();
        }

        // After timing out (or finishing), wait for remaining tasks
        for (int ti = 0; ti < thread_count; ++ti)
        {
            if (futures[ti].valid())
                futures[ti].get();
        }

        if (current_tile >= tile_count)
        {
            current_tile = 0;
            return true;
        }
        return false;
    }

    template<typename WorldT, typename Callback>
    bool forEachWorldTilePixel(
        int tile_w, int tile_h,
        TileBlockProgress& P, // progress tracker
        Callback&& callback,
        int thread_count = Thread::threadCount(),
        int budget_ms = 16, // 0 = no timeout (finish in this call)
        int block_w = 64,
        int block_h = 8
    )
    {
        using namespace detail;

        ensureBlocksBuilt(P, raster_w, raster_h, tile_w, tile_h, block_w, block_h);

        // initialize cursors if thread_count changed
        P.ensure_owner_slots(thread_count);

        const int N = static_cast<int>(P.blocks.size());
        if (N == 0) return true;

        const bool no_timeout = (budget_ms == 0);
        const double t_end = no_timeout ? std::numeric_limits<double>::max() : (now_ms() + double(budget_ms));

        // world quad
        Quad<WorldT> world_quad = static_cast<Quad<WorldT>>(WorldObjectT<T>::worldQuad());
        const WorldT ax = world_quad.a.x, ay = world_quad.a.y;
        const WorldT bx = world_quad.b.x, by = world_quad.b.y;
        const WorldT cx = world_quad.c.x, cy = world_quad.c.y;
        const WorldT dx = world_quad.d.x, dy = world_quad.d.y;

        const WorldT t_bmp_w = static_cast<WorldT>(raster_w);
        const WorldT t_bmp_h = static_cast<WorldT>(raster_h);

        //blPrint() << "ay: " << bl::to_string(ay);

        // one persistent task per worker
        std::vector<std::future<void>> futs;
        futs.reserve(thread_count);

        for (int k = 0; k < thread_count; ++k)
        {
            futs.push_back(Thread::pool().submit_task([&, owner_id = k]()
            {
                int M = P.owner_count;              // threads used for this plan
                int cur = P.owner_cursor[owner_id]; // blocks already consumed by this owner
                auto idx_from_cur = [&](int c) {
                    return owner_id + c * M;
                };

                while (true)
                {
                    // stop between blocks only
                    if (!no_timeout && now_ms() >= t_end)
                        break;

                    int bi = idx_from_cur(cur);
                    if (bi >= N)
                        break; // this owner finished its sequence

                    const TileBlock b = P.blocks[bi];
                    const int x0 = b.x0, x1 = b.x1, y0 = b.y0, y1 = b.y1;

                    // render micro-block
                    for (int row = y0; row < y1; ++row)
                    {
                        const WorldT bmp_fy = static_cast<WorldT>(row) + WorldT{ 0.5 };
                        const WorldT v = bmp_fy / t_bmp_h;

                        const WorldT scan_left_x = ax + (dx - ax) * v;
                        const WorldT scan_left_y = ay + (dy - ay) * v;
                        const WorldT scan_right_x = bx + (cx - bx) * v;
                        const WorldT scan_right_y = by + (cy - by) * v;

                        for (int bmp_x = x0; bmp_x < x1; ++bmp_x)
                        {
                            const WorldT bmp_fx = static_cast<WorldT>(bmp_x) + WorldT{ 0.5 };
                            const WorldT u = bmp_fx / t_bmp_w; // TODO: Not efficient for fdd WorldT. Either cache or calculate offset in double precision

                            const WorldT wx = scan_left_x + (scan_right_x - scan_left_x) * u;
                            const WorldT wy = scan_left_y + (scan_right_y - scan_left_y) * u;

                            if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT, int>)
                                std::forward<Callback>(callback)(bmp_x, row, wx, wy, b.tile_index);
                            else if constexpr (std::is_invocable_r_v<void, Callback, int, int, WorldT, WorldT>)
                                std::forward<Callback>(callback)(bmp_x, row, wx, wy);
                        }
                    }
                    ++cur; // advance owner's local cursor
                }

                // write back owner's progress
                P.owner_cursor[owner_id] = cur;
            }));
        }

        for (auto& f : futs)
            f.get();

        bool finished = true;
        for (int k = 0; k < P.owner_count; ++k) {
            const int bi = k + P.owner_cursor[k] * P.owner_count;
            if (bi < N) { finished = false; break; }
        }

        if (finished) {
            P.reset_progress_only();
            return true;
        }
        return false;
    }


};

typedef WorldRasterGridT<f64>  WorldRasterGrid;
typedef WorldRasterGridT<fdd> WorldRasterGrid128;
typedef WorldRasterGridT<fqd> WorldRasterGrid256;

BL_END_NS;
