# Checkpoints And Snapshots

## Contents

- Checkpoint model, saved state, and economics
- Headless checkpoint flags
- Canvas snapshots, custom image routes, and capture ownership

Use this page when adding restartable state or visual diagnostic snapshots.

## Checkpoint Model

Bitloop checkpoints are explicit, chunked simulation state. They are safer than raw process memory snapshots and do not automatically serialize arbitrary live memory.

Implement:

```cpp
bool MyScene::saveCheckpoint(SimCheckpointWriter& writer) const
{
    writer.writeJson("state", JSON::json({
        {"time", simulation_time}
    }));
    writer.writeTrivialVector("particles", particles);
    return true;
}

bool MyScene::loadCheckpoint(const SimCheckpointReader& reader)
{
    auto state = reader.readJson("state");
    simulation_time = state["time"].get<double>();
    particles = reader.readTrivialVector<Particle>("particles");
    return true;
}
```

Use chunks for large arrays so restore can load only defined state and metadata can describe what is present.

Checkpointing is a precision tool, not a default. Use it when reaching the interesting state by warmup/replay is more expensive than saving and restoring that state across the expected number of reruns.

## What To Save

Save the minimal deterministic state needed to resume:

- simulation time/frame
- random seeds and generator state
- particles/bodies/fields/grids
- configuration values that affect evolution
- cached data only if expensive and deterministic to store

Do not save UI-only state unless it affects simulation behavior after restore.

For deep zooms or late-stage generated worlds, prefer small semantic checkpoints such as camera position, zoom, frame, seed, selected handles, active chunks, and deterministic generator state. Avoid full massive-state checkpoints unless the economics justify the disk and load cost.

## Checkpoint Economics

Before choosing checkpoints for a heavy workflow, estimate or measure:

- warmup seconds to reach the state
- save seconds
- load seconds
- checkpoint bytes
- expected reruns from that state

Prefer a checkpoint when:

```text
warmup_seconds * expected_reruns > save_seconds + (load_seconds * expected_reruns)
```

Also consider storage pressure, determinism risk, and whether the checkpoint can stay small by saving semantic restart state instead of all derived data.

## Headless Checkpoint Flags

Useful flags:

```powershell
--checkpoint-every-frames 300
--checkpoint-at 1200
--checkpoint-dir C:\path\to\checkpoints
--restore-checkpoint C:\path\to\checkpoint
```

Before using checkpoint-based reruns as behavioral evidence, compare an uninterrupted run through `K + M` ticks with a run saved at `K`, restored, and advanced for the same `M` ticks. Compare frame/time, hard invariants, important semantic telemetry, RNG state when observable, and deterministic fingerprints or justified tolerances. A checkpoint that merely loads supplies a smoke test, not continuation equivalence.

Headless checkpoint metadata records stable selected Scene IDs and the launch-config override hash. Restore requires the same deterministic Scene-ID set and override hash; a mismatch fails explicitly before checkpoint state is applied.

## Trace Canvas Snapshots

Scenes can request visual snapshots:

```cpp
traceCanvas("overview", "periodic check");
traceCanvas("collision", IRect(x, y, w, h), "region around collision");
traceCanvasWorld("collision_world", DRect(x1, y1, x2, y2), "world-space collision area");
traceCanvas(viewport, "isolated", "render only this Viewport");
traceCanvasWorld(viewport, "isolated_world", world_rect, "world crop from this Viewport");
```

Snapshots are diagnostic requests, not semantic truth. Pair them with targeted trace metrics or events when possible.

Viewport-targeted requests render only that Viewport, not its neighbours in the Layout. Stable Scene and Viewport IDs are written to snapshot metadata. A Scene-only target succeeds when exactly one matching Viewport exists, fails when several are ambiguous, and fails with guidance when none exists.

Hidden Viewports can be captured at their `setRenderSize(IVec2)` resolution. Normal periodic Project snapshots still render the complete visible Layout. Isolated rendering temporarily supplies the requested geometry, rebinds its camera, draws it through the normal Viewport path, and restores the previous geometry.

Use snapshots sparingly for massive simulations:

- periodic overview snapshots for visual progression
- local pixel/world rect snapshots around suspicious events
- snapshots immediately before/after a collision or state transition

## Custom Image Routes

Use a custom route when inspection needs native grid pixels, a heatmap, or another project-generated image at its own resolution. Use `traceCanvas` for ordinary views and crops; `--surface` changes the layout canvas size, and rectangle requests crop existing canvas pixels.

Under `BITLOOP_DEV_MODE`, override the Scene hook:

```cpp
bool MyScene::captureTraceSnapshot(std::string_view route, SimSnapshotImage& image) const
{
    if (route != "grid")
        return false;
    image.resolution = IVec2(grid_width, grid_height);
    image.pixels = grid_rgba;
    return true;
}
```

`pixels` owns exactly `width * height * 4` tightly packed RGBA8 bytes, with the top row first. Fill it from current state without advancing the simulation. Return false for unsupported routes or capture failure. CPU sources can fill pixels directly; GPU sources can render/read their own texture using `CapturePreprocessor` on the render owner thread, choosing `flip_y` for that texture's row orientation.

Select periodic routes with `--snapshot-every-frames N --snapshot-route primary:grid`, where `primary` is an active Scene ID. The route flag is repeatable; without it, periodic snapshots capture the visible layout. Scene code can also request `traceSnapshot("grid", "interface changed")` and run with `--snapshots`.

The framework invokes the hook only for enabled capture requests, after frame processing and before the next frame. Its schedule is independent of semantic `traceFrame` collection. Custom images reuse lossless WebP encoding, embedded metadata, and `snapshots.jsonl`, including the route, Scene ID, frame/time, and actual dimensions. `SimSnapshotImage::payload` optionally supplies the image's XMP save string. Routes need no additional Viewports; hidden Viewports also process and can duplicate viewport-owned simulation updates.

## Native Toolbar Snapshots

For the ordinary Snapshot button, override this Scene hook (available in regular builds):

```cpp
bool MyScene::captureSnapshot(SnapshotFrame& output) const
{
    output.resolution = {grid_width, grid_height};
    output.frame.data = grid_rgba;
    output.frame.payload = serializeCurrentState();
    return true;
}
```

`data` owns tightly packed RGBA8 pixels, top row first. Capture pixels and any state payload together on this caller: the simulation worker invokes the hook after a completed update, including while paused, then transfers the owned result to the GUI before starting the existing encoder. It must not advance the simulation. Marshal backend readback to its required owner if necessary. No capture readback or serialization runs on ordinary frames.

Return false to retain viewport/preset capture, or throw to report a capture error. The default Project hook delegates only when exactly one Scene is active; composite Projects keep the viewport fallback unless they override `ProjectBase::captureSnapshot`. The encoder uses the supplied dimensions and normal capture settings; existing `onEncodeFrame` callbacks still run on the GUI owner with the native resolution and can postprocess the prepared frame. A nonempty payload is embedded using the existing WebP XMP save-string format; image-based state restoration remains project-owned. Native images do not depend on camera, viewport overlays or preset sizing.

## Capture Pipeline

Bitloop has capture and snapshot support through project/headless orchestration. For project-specific rendered assets, use existing capture callbacks and metadata hooks rather than adding a separate image-writing pipeline.

GL resource creation/destruction should remain on the GUI/render owner thread. Use `ThreadQueue` where needed.

## Current Boundary

Do not design new features around raw memory dumps unless the user explicitly asks for an architecture discussion. The current safe model is explicit serialization plus targeted visual snapshots.
