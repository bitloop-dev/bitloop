# Threading

Use this page when parallelizing simulation work.

## Shared Pool

Bitloop exposes thread helpers in `include/bitloop/core/threads.h`.

Useful calls:

```cpp
int n = Thread::threadCount();
Thread::setMaxThreads(0);
auto& pool = Thread::pool();
```

`setMaxThreads(0)` restores automatic sizing. The automatic thread count is based on hardware concurrency with platform-specific caps.

## Batch Processing

Use `Thread::forEachBatch` for parallel loops over ranges or spans.

Mutable span batches:

```cpp
Thread::forEachBatch(particles, [&](std::span<Particle> batch) {
    for (Particle& p : batch)
    {
        // safe: each particle belongs to one batch
    }
});
```

Mutable span with thread index:

```cpp
Thread::forEachBatch(particles, [&](std::span<Particle> batch, int thread_index) {
    // use thread_index for per-thread scratch data
});
```

Index ranges over a vector:

```cpp
Thread::forEachBatch(particles, [&](int i0, int i1) {
    for (int i = i0; i < i1; ++i)
    {
        Particle& p = particles[i];
    }
});
```

Read-only spans:

```cpp
Thread::forEachBatch(values, [&](std::span<const Value> batch) {
});
```

Some overloads collect return values from each batch into a vector. Use this for reductions instead of contended shared writes.

## Pool Tasks

For a fixed set of independent tasks, submit directly to the shared pool:

```cpp
auto future = Thread::pool().submit_task([&] {
    return compute();
});
```

Wait on futures at a deterministic point before using results.

## Safety Rules

- Partition writes by batch whenever possible.
- Do not write the same object from multiple batches without explicit synchronization.
- Prefer immutable snapshots for collision broad-phase or neighbor reads.
- Accumulate per-batch results and merge afterward.
- Do not call ImGui from worker code.
- Do not create or destroy GL resources from arbitrary worker threads; post GUI-thread work through `ThreadQueue`.
- Keep trace/debug output out of tight per-particle loops unless the trace is explicitly needed and bounded.

## When To Use It

Use threading for expensive independent work: particle integration, rasterization, collision broad phase, image generation, and many independent simulations. Keep small simulations single-threaded until the cost justifies the complexity.


