# Camera And Viewport

Use this page when setting up views, mapping coordinates, or adding camera interaction.

## Scene Mount Setup

Camera setup usually belongs in `sceneMounted(Viewport* ctx)` because the viewport surface is known there.

Typical setup:

```cpp
camera.setOriginViewportAnchor(Anchor::CENTER);
camera.focusWorldRect(*ctx, DRect(-10, -10, 10, 10));
camera.uiSetCurrentAsDefault();

navigator.setTarget(camera);
navigator.setDirectCameraPanning(true);
```

Use project-specific world bounds and anchors. For screen-like games such as Pong, a centered world rect or a fixed playfield rect is often simpler than deep camera navigation.

For direct-manipulation projects, keep the camera fixed unless the user asks for navigable world movement. Camera navigation should not consume the same pointer/touch gestures needed for the project's domain interaction.

Camera state is independent of Scene, Viewport, and canvas ownership. A Viewport begins with an identity world transform, and a Scene opts into projection while drawing. Surface-dependent operations take the relevant Viewport explicitly.

If one Camera is shared by several Viewports, pan/zoom/rotation state stays synchronized while each Viewport derives projection from its own dimensions. Fit that shared Camera against one intentional reference Viewport; use separate Cameras when each Viewport needs an independently fitted or navigated view.

## Layout And Placement

`layout << scene` remains the shortest automatic-grid form. Use `layout.add(scene, "stable-viewport-id")` when later lookup, isolated snapshots, or multiple views of one Scene need a stable Viewport ID.

Visible automatic Viewports consume responsive grid cells. Explicitly placed and hidden Viewports do not:

```cpp
Viewport& main = layout.add(scene, "main");
main.setPlacement(ViewportPlacement::fill());

Viewport& map = layout.add(scene, "map");
map.setPlacement(ViewportPlacement::anchored(
    Anchor::BOTTOM_RIGHT,
    DVec2{ 320.0, 180.0 },
    DVec2{ 16.0, 16.0 }));
map.setZIndex(10);
```

Placement policies are `automatic()`, `fill()`, `normalized(DRect)`, and `anchored(Anchor, DVec2 size, DVec2 margin)`. Manual placement persists across resizes. Equal z-indices use insertion order.

Use `setVisible(false)` for a mounted offscreen Viewport and `setRenderSize(IVec2)` to define its capture size. Hidden Viewports process but are not drawn or hit-tested. Visible Viewports use their resolved presentation size.

## Drawing Transform

Apply the Scene's chosen camera in `viewportDraw()`:

```cpp
ctx->transform(camera);
```

Without that call, drawing uses the normal identity canvas transform. `ctx->transform(camera)` composes with the current transform; `ctx->setTransform(camera)` replaces it. This keeps cameras optional and permits additional local or camera transforms when useful.

For HUDs and labels, use `stageMode()` or `worldHudMode()` as described in `drawing.md`.

## Pointer To World Coordinates

Use Bitloop transforms rather than manual viewport math:

```cpp
DVec2 world = e.worldPosition(camera);
```

Only handle pointer input when the event belongs to the scene or viewport:

```cpp
if (!e.ownedBy(this))
    return;
```

## Navigation

Prefer `CameraNavigator` for panning/zooming:

```cpp
if (navigator.handleWorldNavigation(e, true))
{
    requestRedraw();
}
```

The navigator uses the event's owning Viewport to project gestures and does not retain a surface. Existing projects sometimes restrict zoom ranges or use direct camera panning. Configure those options in `sceneMounted`.

If free camera movement would conflict with object/location manipulation, omit `CameraNavigator` from the scene or gate it behind an explicit navigation mode.

## Redraws

When camera or view settings change, request redraw rather than continuously repainting. Existing projects often use:

```cpp
if (Changed(camera, options))
{
    requestRedraw();
}
```

Use `requestImmediateUpdate()` when interaction or progressive computation needs another update frame immediately.

## Precision

For ordinary simulations, `DVec2`/`f64` camera coordinates are enough. Use `f256_s`/`QDVec2` camera state only for deep zoom or numerically sensitive views. Convert carefully when sending data to shaders or drawing APIs that use lower precision.
