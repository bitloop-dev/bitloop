# Tracing And Diagnostics

## Contents

- Stable versus temporary telemetry
- Indexed run folders and headless execution
- Trace hooks and compact JSONL
- Reconstruction, ephemeral health data, snapshots, and cleanup

Use this page when adding semantic telemetry, running headless traces, reconstructing compact frames, or managing Codex run data.

## Principle

Generated simulations must explicitly report semantic state. Do not infer telemetry from pixels in v1. Keep telemetry as small as possible and targeted to the current task.

Do not trace every new object by default. Add telemetry when it is needed to verify behavior, diagnose a bug, implement a feature, or monitor an important outcome.

Use stable trace output for telemetry that should remain useful across future tasks. Use `temporary-diagnostics.md` for disposable probes that should compile only under `BITLOOP_DEV_MODE` and be removed after the current objective.

## Run Folder Convention

For Codex-created projects, retained diagnostic evidence belongs under:

```text
<ProjectRoot>\.codex\runs\
```

Each run gets a sequential folder:

```text
run0000_YYYYMMDD_HHMMSS
run0001_YYYYMMDD_HHMMSS
```

Inspect existing folders matching `run<index>_*` or `run<index>`, choose highest index plus one, and start at `0` when none exist. Do not use descriptive run folder names. Put descriptive context in metadata or summaries inside the run folder. Do not delete previous runs unless the user asks.

This convention is for evidence worth retaining across the objective. High-volume raw state, generated analyzers, and intermediate data intended for immediate reduction belong in a marked ephemeral session under the Codex build directory instead. Follow `simulation-health-diagnostics.md`; keep only compact conclusions or a deliberately selected reproducer as retained evidence.

## Headless Command

List a Project's declarative Scene catalogue without constructing any Scenes:

```powershell
app.exe --headless --project <ProjectName> --list-scenes
```

Give reusable scenarios stable IDs with `create<MyScene>("scene-id", config)`. Select one or more exact IDs with repeatable `--scene`; an explicit selection replaces the Project defaults:

Syntax example (`<frame-count>` must be chosen from the validation claim; it is not a recommended horizon):

```powershell
app.exe --headless --project <ProjectName> --scene primary --frames <frame-count> --dt 0.0166667 --surface 800x600 --trace <ProjectRoot>\.codex\runs\run0000_20260628_120000
```

Useful flags include:

- `--frames`
- `--list-scenes`
- `--scene ID` (repeatable)
- `--scene-config FILE`
- `--dt`
- `--surface`
- `--trace`
- `--max-objects`
- `--trace-format full|compact`
- `--trace-every-frames N`
- `--trace-at A,B,C`
- `--trace-keyframe-frames`
- `--snapshots`
- `--snapshot-every-frames`
- `--snapshot-every-seconds`
- `--checkpoint-every-frames`
- `--checkpoint-at`
- `--checkpoint-dir`
- `--restore-checkpoint`

Use fixed `dt` for deterministic iteration.

`--scene-config` requires exactly one `--scene` and a top-level JSON object. The selected typed handle must opt in during `projectPrepare()`:

```cpp
auto scene = create<MyScene>("primary", config);
scene.setLaunchConfigParser(
    [](MyScene::Config& config, const JSON::json& values)
    {
        // Reject unknown fields, wrong types, invalid ranges, and invariants here.
    });
```

The parser runs against the retained typed `Config` before the Scene constructor. Bitloop provides no generic reflection or `--set` fallback. Run metadata records the selected Scene IDs, supplied JSON, and its stable hash.

### Semantic trace schedule

Trace scheduling controls calls to `Scene::traceFrame(...)` and their serialization, not simulation advancement. Input dispatch, `processHeadlessFrame(...)`, `traceCanvas(...)` requests, visual snapshots, and checkpoints continue on every applicable processed frame.

- With neither scheduling flag, Bitloop traces every processed frame. This is the backward-compatible default.
- `--trace-at A,B,C` alone traces only those exact absolute frame indices.
- `--trace-every-frames N` traces the first processed frame of the run and every `N` processed frames after it. `0` disables periodic semantic tracing.
- Supplying both uses the union, without duplicate observations.
- Frame indices are absolute, zero-based, and post-process. After checkpoint restore, the periodic cadence begins at that run's restored start frame; `--trace-at` values remain absolute and must lie inside the run range.

Prefer every-frame tracing for short runs, new or uncertain telemetry, transient/event-sequence diagnosis, exact frame-to-frame comparisons, and any run where no independent monitor protects the gaps. Prefer sparse tracing when semantic collection or serialization is materially expensive and cheap per-tick monitors still evaluate the hard failure conditions across the complete horizon. Use `--trace-every-frames 0` only when semantic state is irrelevant to the current gate or another independent oracle covers it; a successful run with no semantic frames is not behavioral evidence by itself.

Choose sparse frames from lifecycle landmarks, expected timescales, forcing/event windows, complete-inspection frames, and the final horizon rather than an arbitrary interval. Begin with coarse coverage, then deterministically rerun suspicious brackets with exact neighboring frames or every-frame tracing. See `simulation-health-diagnostics.md` for horizon and schedule selection.

`metadata.json` records the effective schedule and observed-frame counts. `summary.json` distinguishes processed frames from trace frames and labels whether trace-derived aggregates cover all, selected, or no semantic frames.

## Trace Hook

Implement:

```cpp
void MyScene::traceFrame(SimTraceFrame& frame) const
{
    if (auto* ball = frame.addObject("ball", "ball", ball_position.x, ball_position.y))
    {
        ball->vx = ball_velocity.x;
        ball->vy = ball_velocity.y;
        ball->radius = ball_radius;
    }

    frame.metric("speed", ball_velocity.mag());
    frame.addEvent("wall_bounce").attribute("wall", "right");
}
```

Objects support id, kind, x/y, optional velocity, radius, angle, mass, tags, and numeric/string attributes. Metrics and events are useful for aggregates and discrete causes.

Prefer stable ids. Stable ids make compact deltas and frame reconstruction dependable.

## Compact JSONL

Headless traces default to compact delta JSONL. Read `metadata.json` first. It declares schema, format, fixed `dt`, project, scenes, effective trace schedule, observed-frame counts, keyframe interval, and short keys used in `frames.jsonl`.

Compact frame keys:

- `f`: frame index
- `s`: scene id
- `k`: keyframe flag/full state
- `o`: object deltas
- `m`: metric deltas
- `mr`: removed metrics
- `e`: events
- `d`: dropped object count

Use `--trace-format full` only when self-contained per-frame JSON is worth the larger files.

## Reconstructing A Frame

For a full semantic state at one frame, use the reusable tool:

```powershell
bitloop trace reconstruct --file .\.codex\runs\<run>\frames.jsonl --frame 54 --pretty
```

The helper creates or reuses a sidecar index beside `frames.jsonl` so random frame lookups can start near the nearest keyframe. For scheduled traces it reconstructs only frames that were actually observed and rejects an unobserved frame rather than silently carrying an earlier state forward as if it were current.

## Full-State Inspection

Do not increase `--max-objects` and serialize every entity merely because a diagnosis needs whole-state knowledge. Prefer a selected in-process committed-state scan that emits aggregate metrics and bounded suspicious IDs. Use raw full snapshots only when a schema-specific local analyzer needs data that cannot be reduced correctly during the run. See `simulation-health-diagnostics.md` for adaptive frame selection, large-state analysis, and cleanup.

## Visual Snapshots

Use `traceCanvas(id, reason)` when visual information is useful in addition to semantic telemetry. Use pixel/world rect overloads for focused regions when full-frame images are too large. See `checkpoints-snapshots.md`.

## Cleanup Between Objectives

Before starting a new objective, remove temporary telemetry that only served the previous diagnosis. Keep stable, likely-useful telemetry such as core object positions, velocities, important collisions, or global metrics.
