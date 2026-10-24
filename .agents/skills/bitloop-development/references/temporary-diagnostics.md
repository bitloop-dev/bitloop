# Temporary Diagnostics

Use this page when a task needs short-lived probes, visibility queries, diagnostic metrics, focused snapshots, or events that should disappear once the objective is solved. For long-horizon anomalies, explicit correctness oracles, or selected whole-state inspections, also read `simulation-health-diagnostics.md`.

## Purpose

Temporary diagnostics are development-only adapters for Codex iteration. They expose just enough state to test the current hypothesis without adding permanent AI metadata, extra simulation ownership, or release-build overhead.

Stable telemetry belongs in `traceFrame(...)` when it is useful across future tasks. Temporary telemetry belongs in a clearly marked diagnostics file or block and should be removed when it stops carrying its weight.

## Compile Boundary

Guard temporary diagnostics with:

```cpp
#ifdef BITLOOP_DEV_MODE
// temporary diagnostics only
#endif
```

Final release and WASM builds must be able to compile with `BITLOOP_DEV_MODE=FALSE` and drop these probes entirely.

## Project Convention

For a small project-local probe, prefer:

```text
<Project>/<Project>/diagnostics/
```

Use a `diagnostics` namespace:

```cpp
#ifdef BITLOOP_DEV_MODE
namespace diagnostics
{
    void traceCurrentProbe(const MyScene& scene, SimTraceFrame& frame);
}
#endif
```

Diagnostics may emit capped trace objects, aggregate metrics, events, and `traceCanvas(...)` requests. They should read existing scene state, production query APIs, camera transforms, and spatial indexes. They should not own authoritative state or require permanent per-object labels.

For a substantial health investigation, keep one temporary monitor header/source pair rather than one file per predicate. Put large raw outputs and generated analyzers in a marked session under the Codex build directory. If temporary source cannot be compiled from there cleanly, use one clearly named adapter under the project diagnostics folder and remove it with every integration call when the objective ends.

## Data Discipline

- Prefer a metric or event over a traced object when the object identity is not important.
- Prefer selected handles, visible candidates, sampled ids, local regions, and aggregate counts when they answer the question.
- When correctness depends on the complete state, scan all authoritative state at deliberately selected committed frames and reduce it in process. Do not confuse a complete inspection with serializing every entity.
- Use `traceCanvas(...)` or rect/crop snapshots when visual evidence is cheaper than serializing many objects.
- Cap every candidate list, and include dropped/count metrics when caps matter.
- Name temporary metrics with a clear prefix such as `diagnostics.` so they are easy to find and remove.

## Large-Scale Rule

Do not make a massive simulation permanently slower or larger just so Codex can inspect it. A selected O(N) diagnostic scan can be the most reliable option even for a large state when its measured cost and information value justify it. Reuse read-only production query boundaries such as spatial bins, chunk ids, selected handles, or event-local neighborhoods; use mergeable chunk reductions; and avoid O(N²) pair work when a spatial method is available. A temporary probe can summarize those APIs, but it should not become a second simulation model.

## Cleanup

At the end of an objective, remove probes, call sites, includes, build wiring, and scratch support that only answered that objective. Clean marked high-volume sessions after reducing them to compact findings. Keep only production safety checks and telemetry that have earned a place in the project's long-term testability.

