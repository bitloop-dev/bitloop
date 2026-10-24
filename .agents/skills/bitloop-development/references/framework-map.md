# Framework Map

Paths on this page are relative to a Bitloop framework source checkout. In a package-backed project, resolve the installed headers as described in `SKILL.md` before inspecting exact declarations.

Use this page when orienting yourself in Bitloop before choosing a specific feature page.

## Umbrella Include

`include/bitloop.h` is the main project include. It pulls in:

- platform/runtime setup
- core project, scene, layout, viewport, camera, events, variables, tracing, checkpoints, snapshots, and threads
- `nanovgx` drawing wrappers
- `imguix` UI helpers
- utility types, math, color, JSON, compression, timers, and thread queues
- `fltx`, with `bl::literals`, for extended floating point types

## Core Areas

- `core/project.h`: Project-owned Scene catalogue, factories, defaults, deferred structural mutation, headless selection/configuration, root paths, and trace/snapshot/checkpoint orchestration.
- `core/scene_handle.h`: generation-checked typed/non-owning Scene handles and typed launch-config parser registration.
- `core/layout.h`, `core/layout_solver.h`: Layout-owned Viewports, responsive automatic/manual placement, stable ordering, visibility, and pointer policy.
- `core/scene.h`: scene lifecycle, per-frame processing, viewport drawing, events, trace hooks, checkpoints, canvas trace requests.
- `core/project_worker.*`: worker-thread loop, safe UI/simulation sync points, project commands, event polling, processing, and end-frame hooks.
- `core/var_buffer.h`: double-buffered UI/live state model and `bl_scoped`, `bl_pull`, `bl_push`, `bl_view`, `bl_schedule`.
- `core/viewport.h`: viewport surface context, placement/input properties, and non-owning camera/Scene bindings; inherits the drawing `Painter`.
- `core/camera.h`: camera transforms, navigation, precision controls, and ImGui camera controls.
- `core/event.h` and `core/input.h`: SDL event wrappers, pointer/key helpers, viewport ownership, and mouse state.
- `core/threads.h`: shared thread pool and batch helpers.
- `core/sim_trace.h`, `core/headless_runner.h`: semantic trace frames and headless execution.
- `core/sim_snapshot.h`, `core/sim_checkpoint.h`: visual trace canvas snapshots and chunked simulation checkpoints.

## Rendering And UI

- `nanovgx/nano_canvas.h`: authoritative drawing API for `Viewport* ctx`.
- `nanovgx/nano_bitmap.h`: CPU images and world-space raster grids.
- `nanovgx/nano_shader_surface.h`: GL shader surfaces and multi-pass surfaces.
- `core/image_loader.h`: image loading and GL texture helpers.
- `imguix/imguix.h`: ImGui extensions such as `CollapsingHeaderBox`.

## Utilities

- `util/thread_queue.h`: posting work to a specific owner thread, useful for GL resource cleanup.
- `util/json.h`: JSON helpers used by traces, metadata, state, and checkpoints.
- `util/change_tracker.h`: `Changed(...)` helpers used to request redraw only when state changes.
- `util/color.h`: `Color` and color helpers.
- `util/timer.h`, `util/debug_timer.h`: timing helpers.
- `util/compression.h`: compression helpers used by capture and persistence flows.

## Extension Points

Most project work should be expressed through existing virtual hooks:

- `ProjectType::projectPrepare(Layout& layout)` for declarative Scene records, Viewports, and default launch selection.
- `SceneType::sceneStart()` for scene initialization.
- `SceneType::sceneMounted(Viewport*)` for viewport/camera setup that needs surface dimensions.
- `SceneType::sceneUnmounted(Viewport*)` for symmetric per-Viewport cleanup.
- `SceneType::sceneProcess()` for project-level simulation updates.
- `SceneType::viewportProcess(Viewport*, double dt)` for viewport-specific simulation or raster/shader updates.
- `SceneType::viewportDraw(Viewport*) const` for drawing only.
- `SceneType::onEvent`, `onPointer*`, `onKey*` for input.
- `SceneType::traceFrame(SimTraceFrame&) const` for semantic telemetry.
- `SceneType::saveCheckpoint` and `loadCheckpoint` for opt-in restart state.

