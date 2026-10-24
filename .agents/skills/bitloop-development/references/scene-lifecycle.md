# Scene Lifecycle

Use this page when deciding where simulation updates, drawing, events, tracing, and setup code belong.

## Project Lifecycle

`ProjectType::projectPrepare(Layout& layout)` declares Scene records, creates their Viewports, and selects optional defaults. Keep it declarative and light; constructors run only for the selected/default launch set, and per-Scene initialization belongs in Scene hooks.

`ProjectBase` owns and schedules Scenes. `Layout` owns Viewports, which refer to Scenes without owning them. A Scene may run with no Viewport, and removing or clearing Viewports does not destroy it.

If no defaults are specified, every declared Scene launches. Large catalogues should give Scenes stable IDs and choose defaults explicitly:

```cpp
void MyProject::projectPrepare(Layout& layout)
{
    auto primary = create<MyScene>("primary");
    auto comparison = create<MyScene>("comparison", comparison_config);

    layout << primary << comparison;
    setDefaultScenes({ primary });
}
```

`SceneHandle<T>` is safe to store. `ready()` and `get()` report whether the selected Scene is materialized; checked dereference throws before construction or after destruction. Do not dereference handles from `projectPrepare()`.

## Scene Hooks

- `sceneStart()`: initialize scene-owned state that does not require a viewport size.
- `sceneMounted(Viewport* ctx)`: initialize viewport-dependent values such as `camera.focusWorldRect(*ctx, ...)` and configure navigation.
- `sceneUnmounted(Viewport* ctx)`: release per-Viewport state. Removing one of several Viewports leaves the Scene running.
- `sceneProcess()`: advance scene-wide simulation state once per project frame.
- `viewportProcess(Viewport* ctx, double dt)`: update work that genuinely depends on a mounted Viewport. Hidden Viewports still receive this hook.
- `viewportDraw(Viewport* ctx) const`: draw the current state only. Do not mutate simulation state here.
- `onEndFrame()`: post-frame hook after processing/draw coordination.
- `sceneStop()` and `sceneDestroy()`: release resources. Post GL cleanup to the render thread queue. Individual Scene removal and Project teardown wait for queued cleanup, including tasks posted by cleanup tasks, before deleting Scenes. Project teardown also waits for cleanup posted by `projectDestroy()`. Stop or join background producers in the hooks so they cannot queue new Scene access after teardown finishes. Destructor-posted work must own its data rather than capture the destroyed object.

Runtime Scene/Viewport creation, removal, and property changes are deferred to safe frame boundaries. Scene construction and simulation lifecycle stay on the worker; UI work stays on the GUI thread. Handles fail safely after destruction.

## Drawing Versus Simulation

Treat drawing as a const projection of current state. Mutating in `viewportDraw` makes GUI and headless behavior harder to reason about, and it can break deterministic traces.

Use `requestRedraw()` when visual output needs repainting after a state change. Use `requestImmediateUpdate()` for interactive or continuous work that should keep the loop active.

## Events

Events are safe places to mutate live scene state when the event belongs to the scene or viewport. Check ownership with `e.ownedBy(this)` or pointer hover with `e.hoveredOver(this)` before acting.

For camera movement, check event ownership and then prefer `CameraNavigator::handleWorldNavigation(e, ...)` rather than hand-rolling panning and zooming.

## Tracing And Checkpoints

Use `traceFrame(SimTraceFrame& frame) const` to emit semantic state that an external agent can inspect. Keep it explicit and minimal.

Use `saveCheckpoint(SimCheckpointWriter&) const` and `loadCheckpoint(const SimCheckpointReader&)` only for state needed to restart the simulation. Return `true` when state was actually saved or loaded. Checkpoints are opt-in and chunked; they are not automatic raw process memory dumps.

Use `traceCanvas(...)` when a visual snapshot would help diagnose progression, layout, or collision behavior.
