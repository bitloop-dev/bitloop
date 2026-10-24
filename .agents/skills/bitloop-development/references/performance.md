# Performance Guidance

Use this page when a simulation may become expensive or when traces/checkpoints risk becoming too large.

## Simulation Loops

- Keep per-frame update work in `sceneProcess` or `viewportProcess`.
- Keep `viewportDraw` as a projection of current state.
- Use compact per-particle structs for large arrays.
- Prefer contiguous vectors for particle/field state.
- Use spatial bins, grids, or broad-phase structures for collision-heavy simulations.
- Use `Thread::forEachBatch` when work is independent and large enough to amortize overhead.

## Rendering

- Draw simple vector objects directly.
- Use `WorldImageT` for CPU rasterized fields or cellular grids.
- Use `ShaderSurface` for GPU-heavy images and progressive rendering.
- Request redraws only when needed for static scenes.
- Use `requestImmediateUpdate()` for progressive or interactive work that must keep advancing.

## UI Sync

Use `Changed(...)` checks when recomputation is driven by UI/camera changes. Avoid rebuilding rasters, shader programs, or large buffers every frame unless inputs changed.

## Diagnostics

Telemetry is not free:

- Trace stable, important objects and metrics only.
- Prefer aggregate metrics for huge systems.
- Use events for discrete causes such as collisions, boundary hits, or mode changes.
- Use `traceCanvas` snapshots for visual summaries when object counts are too large to serialize.
- Use compact JSONL and reconstruct frames with the Bitloop helper.
- When per-frame `traceFrame(...)` work or serialization is material, use `--trace-every-frames` and/or `--trace-at` to observe selected frames while the simulation still advances every frame. Measure before and after; do not assume JSON size is the dominant cost.
- Sparse semantic tracing is safe only when the omitted intervals cannot hide the validation claim or independent per-tick monitors cover that risk. Rerun detected windows densely instead of choosing one permanently conservative cadence.
- Put task-specific probes behind `BITLOOP_DEV_MODE` and a `diagnostics` namespace, then remove them when the task is complete.
- When correctness needs the whole state, prefer selected in-process scans with mergeable reductions over serializing every entity. Measure one scan, then choose an adaptive schedule whose diagnostic value justifies its cost.
- Remove temporary trace variables after the objective is achieved.

## Checkpoints

Checkpoints should save restart state, not every derived cache. For large simulations, chunk arrays and include metadata so restore can validate shape/version before reading. Use checkpoints when warmup/replay cost across expected reruns is higher than save/load cost; otherwise replay from the beginning.

## Massive Simulations

For million-to-billion scale domains:

- Track aggregates, sampled ids, or local regions during ordinary runs. Selected O(N) complete-state inspections remain valid when a hard question requires them; emit summaries and bounded exemplars rather than permanent per-entity telemetry.
- Avoid per-entity debug conditionals in hot loops unless compile-time disabled or explicitly bounded.
- Merge chunk-local reducers and reuse read-only spatial indexes; do not turn thresholded pair detection into an O(N²) scan.
- Serialize raw state only when post-hoc analysis needs it, then stream or map one chunk/snapshot at a time in an ephemeral build session.
- Prefer deterministic checkpoints for fast restarts only when checkpoint economics justify them.
- Add targeted diagnostics around known events or regions rather than global logging.
- Be cautious about architectural changes that require per-particle extra memory.

## Dependency Judgment

Prefer Bitloop utilities and straightforward standard C++ for simple loops, fields, drawing, and data transforms. Consider a project-local dependency only when it provides a substantial algorithmic, physics, numeric, format, or performance capability that would be costly or risky to implement correctly in the project.

Do not add dependencies for minor convenience helpers, speculative future scale, or work that existing Bitloop APIs already cover well.
